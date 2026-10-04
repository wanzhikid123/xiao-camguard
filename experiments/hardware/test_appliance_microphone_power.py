"""Check actual PDM stop/start, still capture and idle recording wake.

Optional authenticated OTA first. Credentials stay in RAM; existing media are
never renamed/deleted and the original capture values are restored.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
from urllib.parse import quote
from appliance_client import Client

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from validate_avi import validate


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address',required=True)
    parser.add_argument('--firmware',type=Path)
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/microphone-power')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    client=Client(args.address,ROOT/'.tools/private/admin-password.txt')
    report=dict(passed=False,checks=[],clips=[],credentials_or_tokens_saved=False)
    original=None;owned_recording=False
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)))
        print('MIC-POWER','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('Microphone power assertion: '+name)
    def wait_for(predicate,seconds):
        until=time.monotonic()+seconds
        while time.monotonic()<until:
            if predicate():return True
            time.sleep(.3)
        return False
    def update(values):
        check(client.request('/api/settings/capture','POST',{'values':values},timeout=30)[0]==200,
              'capture_settings_saved_'+str(len(report['checks'])))
    def files():
        folder=time.strftime('%Y%m%d',time.gmtime());cursor='';items={}
        for _ in range(32):
            data=client.state('/api/media?date='+folder+('&before='+quote(cursor,safe='') if cursor else ''))
            items.update({item['path']:item['bytes'] for item in data['items']})
            if not data['more']:return items
            cursor=data['nextBefore']
        raise RuntimeError('Enumeration bound')
    def clip(label):
        nonlocal owned_recording
        before=files();owned_recording=client.request('/api/record/start','POST')[0]==202
        check(owned_recording,'start_'+label)
        check(wait_for(lambda:client.state()['recording'] and client.state()['audioInputRunning'],20),
              'actual_pdm_recording_'+label)
        time.sleep(8)
        check(client.request('/api/record/stop','POST')[0]==202,'stop_'+label)
        check(wait_for(lambda:not client.state()['recording'],40),'finalized_'+label);owned_recording=False
        after=files();added=[p for p in after if p not in before and p.endswith('.avi')]
        check(len(added)==1,'one_owned_video_'+label);path=added[0];target=args.out/(label+'.avi')
        check(client.download('/api/media/download?path='+quote(path,safe=''),target)==after[path],
              'whole_download_'+label)
        probe=json.loads(subprocess.run(['C:/Program Files/ffmpeg/bin/ffprobe.exe','-v','error',
            '-show_streams','-show_format','-of','json',str(target)],capture_output=True,text=True,
            check=True,timeout=60).stdout)
        result=validate(probe)
        decoded=subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror',
            '-i',str(target),'-f','null','-'],capture_output=True,timeout=90)
        check(result['passed'] and result['duration_comparison_available'] and decoded.returncode==0,
              'complete_mjpeg_pcm_decode_'+label)
        report['clips'].append(dict(path=path,bytes=after[path],durations_s=result['durations_s'],
                                   difference_s=result['duration_difference_s']))
    try:
        client.login();check(True,'chosen_account_login')
        original=client.state('/api/settings/capture')['values']
        check(not client.state()['recording'] and not client.state()['recordRequested'],'no_active_recording')
        baseline=files()
        if args.firmware:
            report['installation']=client.install(args.firmware)
            check(client.state('/api/settings/capture')['values']==original,'ota_preserves_all_capture_values')
            check(client.state()['cameraReady'] and client.state()['sdReady'] and
                  client.state('/api/wireguard')['peerSessionUp'],'ota_preserves_camera_card_wireguard')
            for name in ('index.html','app.js','i18n.js','style.css'):
                route='/' if name=='index.html' else '/'+name
                check(client.request(route,authorized=False)[2]==(ROOT/'ui/appliance'/name).read_bytes(),
                      'deployed_asset_matches_'+name)
        update({'powerSave':0,'micGain':5,'cpuMHz':240,'videoResolution':10})
        check(wait_for(lambda:client.state()['audioInputRunning'] and not client.state()['captureIdle'],10),
              'enabled_microphone_actually_running')
        update({'micGain':0})
        check(wait_for(lambda:not client.state()['audioInputRunning'],5),'off_actually_stops_pdm_input')
        check(client.state()['cameraReady'] and not client.state()['captureIdle'],'mic_off_keeps_camera_active')
        code,_,body=client.request('/api/camera/still',timeout=30)
        check(code==200 and body.startswith(b'\xff\xd8') and body.endswith(b'\xff\xd9'),
              'still_available_with_mic_off')
        check(client.request('/api/camera/photo','POST')[0]==201,'saved_photo_available_with_mic_off')
        check(client.request('/api/record/start','POST')[0]==409,'audio_recording_requires_enabled_mic')
        update({'micGain':5})
        check(wait_for(lambda:client.state()['audioInputRunning'],5),'reenable_actually_restarts_pdm')
        clip('mic-reenabled')
        update({'powerSave':1,'idleSeconds':30})
        check(wait_for(lambda:client.state()['captureIdle'] and not client.state()['audioInputRunning'],45),
              'idle_actually_stops_camera_and_pdm')
        check(client.state('/api/wireguard')['peerSessionUp'],'wireguard_during_real_audio_idle')
        clip('mic-idle-wake')
        final_files=files()
        check(all(final_files.get(p)==n for p,n in baseline.items()),'all_preexisting_media_preserved')
        report['passed']=True
    except Exception as error:
        report['error']=type(error).__name__+': '+str(error);print(report['error'],flush=True)
    finally:
        try:
            if owned_recording:
                client.request('/api/record/stop','POST');wait_for(lambda:not client.state()['recording'],40)
            if original is not None:
                check(client.request('/api/settings/capture','POST',{'values':original},timeout=30)[0]==200,
                      'all_original_capture_values_restored')
            client.request('/api/auth/logout','POST')
        except Exception as error:
            report.update(passed=False,restore_error=type(error).__name__)
        client.clear();report['check_count']=len(report['checks'])
        (args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
