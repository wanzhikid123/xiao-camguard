"""Focused final-image checks: storage reserve, idle audio wake and large video.

Only this run's media are used for rename guards. Existing files stay untouched.
No card is filled, formatted or deleted. Capture values are always restored.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
from urllib.parse import quote
from urllib.request import Request
from appliance_client import Client
from baseline_http import HTTP

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from validate_avi import validate


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address',required=True)
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/refinement-edges')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    client=Client(args.address,ROOT/'.tools/private/admin-password.txt')
    report=dict(passed=False,checks=[],clips=[],credentials_or_tokens_saved=False)
    original=None;owned_recording=False
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)))
        print('REFINE-EDGE','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('Final-image assertion: '+name)
    def update(values):
        code,_,_=client.request('/api/settings/capture','POST',{'values':values},timeout=30)
        check(code==200,'capture_settings_saved_'+str(len(report['checks'])))
    def files():
        folder=time.strftime('%Y%m%d',time.gmtime());cursor='';items={}
        for _ in range(32):
            data=client.state('/api/media?date='+folder+('&before='+quote(cursor,safe='') if cursor else ''))
            items.update({item['path']:item['bytes'] for item in data['items']})
            if not data['more']:return items
            cursor=data['nextBefore']
        raise RuntimeError('Enumeration bound')
    def wait_for(predicate,seconds):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            if predicate():return True
            time.sleep(.3)
        return False
    def clip(label,width,height):
        nonlocal owned_recording
        before=files();code,_,_=client.request('/api/record/start','POST');owned_recording=code==202
        check(owned_recording,'start_'+label)
        check(wait_for(lambda:client.state()['recording'],20),'recording_'+label)
        time.sleep(8)
        payload=b'\0'*288
        code,_,_=client.request('/api/ota','POST',raw=payload,headers={'Content-Type':'application/octet-stream','X-Firmware-SHA256':hashlib.sha256(payload).hexdigest()})
        check(code==409,'ota_guard_during_recording_'+label)
        check(client.request('/api/record/stop','POST')[0]==202,'stop_'+label)
        check(wait_for(lambda:not client.state()['recording'],40),'finalized_'+label);owned_recording=False
        after=files();added=[path for path in after if path not in before and path.endswith('.avi')]
        check(len(added)==1,'one_owned_video_'+label);path=added[0];file=args.out/(label+'.avi')
        check(client.download('/api/media/download?path='+quote(path,safe=''),file)==after[path],'complete_download_'+label)
        probe=json.loads(subprocess.run(['C:/Program Files/ffmpeg/bin/ffprobe.exe','-v','error','-show_streams','-show_format','-of','json',str(file)],capture_output=True,text=True,check=True,timeout=60).stdout)
        validated=validate(probe);video=next(s for s in probe['streams'] if s['codec_type']=='video')
        check(video['width']==width and video['height']==height,'actual_video_dimensions_'+label)
        decoded=subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror','-i',str(file),'-f','null','-'],capture_output=True,timeout=90)
        check(validated['passed'] and validated['duration_comparison_available'] and decoded.returncode==0,'complete_mjpeg_pcm_decode_'+label)
        check(client.state('/api/wireguard')['peerSessionUp'],'wireguard_after_'+label)
        report['clips'].append(dict(path=path,bytes=after[path],width=width,height=height,durations_s=validated['durations_s'],difference_s=validated['duration_difference_s']))
        return path
    try:
        client.login();check(True,'chosen_account_login');settings=client.state('/api/settings/capture');original=settings['values']
        check(not client.state()['recording'] and not client.state()['recordRequested'],'device_idle_before_gate')
        baseline=files();update({'minimumFreeMB':200000,'powerSave':0})
        check(client.request('/api/camera/photo','POST')[0]==507,'reserve_blocks_photo_without_filling_card')
        check(client.request('/api/record/start','POST')[0]==507,'reserve_blocks_video_without_filling_card')
        check(files()==baseline,'reserve_refusal_preserves_all_media')
        update({'minimumFreeMB':original['minimumFreeMB'],'powerSave':1,'idleSeconds':30,'videoResolution':10,'micGain':5,'cpuMHz':240})
        check(wait_for(lambda:client.state()['captureIdle'],45),'camera_enters_real_standby')
        check(client.state('/api/wireguard')['peerSessionUp'],'wireguard_reachable_during_standby')
        path=clip('standby-audio-wake',640,480)
        # Use this gate's own file and an unchanged name. A missing transfer guard
        # cannot rename any preexisting user file or alter this file's content.
        with HTTP.open(Request(client.base+'/api/media/download?path='+quote(path,safe=''),headers={'Cookie':client.cookie}),timeout=10) as stream:
            stream.read(4096)
            label=path.rsplit('/',1)[-1][16:].rsplit('.',1)[0]
            check(client.request('/api/media/rename','POST',{'path':path,'name':label})[0]==409,'rename_in_use_download_refused')
        time.sleep(6)
        update({'powerSave':0})
        for width,height in ((1280,720),(1600,1200)):
            resolution=next(x['value'] for x in settings['videoResolutions'] if x['width']==width and x['height']==height)
            update({'videoResolution':resolution});clip(str(width)+'x'+str(height),width,height)
        check(all(files().get(path)==size for path,size in baseline.items()),'all_preexisting_files_preserved')
        report['passed']=True
    except Exception as error:
        report['error']=type(error).__name__+': '+str(error);print(report['error'],flush=True)
    finally:
        try:
            if owned_recording:
                client.request('/api/record/stop','POST');wait_for(lambda:not client.state()['recording'],40)
            if original is not None:
                code,_,_=client.request('/api/settings/capture','POST',{'values':original},timeout=30)
                report['settings_restored']=code==200
                if code!=200:report['passed']=False
            client.request('/api/auth/logout','POST')
        except Exception:
            report.update(passed=False,restore_error='Capture settings require follow-up')
        client.clear();report['check_count']=len(report['checks']);(args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
