"""Actual supported-clock standby wake through still, settings and audio recording.

Runs the normal firmware, preserves existing media and all capture values,
and fully decodes both newly owned MJPEG/PCM recordings.
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
    parser.add_argument('--clocks',type=int,nargs='+',choices=(80,160,240),default=(80,160))
    parser.add_argument('--wake-modes',nargs='+',choices=('still','settings','photo','record'),default=('still','settings','record'))
    parser.add_argument('--sensor-standby',action='store_true',help='Require actual OV3660 power-down readback and a fresh frame-start timestamp')
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/low-clock-wake')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    client=Client(args.address,ROOT/'.tools/private/admin-password.txt')
    report=dict(passed=False,checks=[],cycles=[],clips=[],photos=[],credentials_or_tokens_saved=False)
    original=None;owned_recording=False
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)))
        print('LOW-CLOCK-WAKE','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('Low clock wake assertion')
    def wait(predicate,seconds):
        until=time.monotonic()+seconds
        while time.monotonic()<until:
            if predicate(client.state()):return True
            time.sleep(.3)
        return False
    def inventory():
        dates=[];cursor='';items={}
        for _ in range(32):
            page=client.state('/api/media/dates'+('?before='+quote(cursor,safe='') if cursor else ''))
            if page['enumerationTruncated']:raise RuntimeError('Date enumeration truncated')
            dates.extend(item['date'] for item in page['items']);cursor=page['nextBefore']
            if not cursor:break
        else:raise RuntimeError('Date enumeration bound')
        for date in dates:
            cursor=''
            for _ in range(64):
                page=client.state('/api/media?date='+date+('&before='+quote(cursor,safe='') if cursor else ''))
                if page['enumerationTruncated']:raise RuntimeError('Media enumeration truncated')
                items.update({item['path']:item['bytes'] for item in page['items']});cursor=page['nextBefore']
                if not cursor:break
            else:raise RuntimeError('Media enumeration bound')
        return items
    def update(values,name):
        code,_,body=client.request('/api/settings/capture','POST',{'values':values},timeout=30)
        if code!=200:report['save_failure']=dict(code=code,error=json.loads(body).get('error','unknown'))
        check(code==200,name)
    try:
        client.login();check(True,'chosen_account_login')
        original=client.state('/api/settings/capture')['values'];baseline=inventory()
        if args.firmware:
            print('LOW-CLOCK-WAKE authenticated candidate installation',flush=True)
            report['installation']=client.install(args.firmware)
            check(client.state('/api/settings/capture')['values']==original,'ota_preserves_all_capture_values')
        initial=client.state();revision=initial['configRevision'];boot_at=time.monotonic()-initial['uptimeSeconds']
        check(not initial['recording'] and not initial['recordRequested'],'no_active_recording')
        for clock in args.clocks:
            update(dict(cpuMHz=clock,powerSave=1,idleSeconds=30,micGain=5,videoResolution=10),'save_clock_'+str(clock))
            for mode in args.wake_modes:
                label=str(clock)+'_'+mode
                check(wait(lambda s:s['captureIdle'] and not s['audioInputRunning'],45),'real_camera_pdm_standby_'+label)
                check(client.state()['cpuMHz']==clock,'selected_clock_before_wake_'+label)
                before_wake=client.state()
                if args.sensor_standby:
                    check(before_wake['cameraSensorSleeping'],'actual_sensor_power_down_'+label)
                started=time.monotonic()
                if mode=='still':
                    code,_,body=client.request('/api/camera/still',timeout=30)
                    check(code==200 and body.startswith(b'\xff\xd8') and body.endswith(b'\xff\xd9'),'jpeg_wake_'+label)
                    path=args.out/(label+'.jpg');path.write_bytes(body)
                    decoded=subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror',
                        '-i',str(path),'-f','null','-'],capture_output=True,timeout=45)
                    check(decoded.returncode==0,'whole_jpeg_decode_'+label)
                elif mode=='settings':
                    update({'cpuMHz':clock},'settings_can_wake_and_save_'+label)
                elif mode=='photo':
                    before=inventory();code,_,body=client.request('/api/camera/photo','POST',timeout=30)
                    check(code==201,'photo_saved_from_standby_'+label);photo=json.loads(body)
                    check(photo['path'] not in before and inventory().get(photo['path'])==photo['bytes'],
                          'one_new_saved_photo_'+label)
                    target=args.out/(label+'.jpg')
                    check(client.download('/api/media/download?path='+quote(photo['path'],safe=''),target)==photo['bytes'],
                          'whole_saved_photo_download_'+label)
                    settings=client.state('/api/settings/capture')
                    expected=next(item for item in settings['photoResolutions'] if item['value']==settings['values']['photoResolution'])
                    probe=json.loads(subprocess.run(['C:/Program Files/ffmpeg/bin/ffprobe.exe','-v','error',
                        '-show_streams','-of','json',str(target)],capture_output=True,text=True,check=True,timeout=30).stdout)['streams'][0]
                    check((probe['width'],probe['height'])==(expected['width'],expected['height']),
                          'actual_saved_photo_dimensions_'+label)
                    decoded=subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror',
                        '-i',str(target),'-f','null','-'],capture_output=True,timeout=45)
                    check(decoded.returncode==0,'complete_saved_photo_decode_'+label)
                    report['photos'].append(dict(path=photo['path'],bytes=photo['bytes'],width=probe['width'],height=probe['height'],clock=clock))
                else:
                    before=inventory();owned_recording=client.request('/api/record/start','POST')[0]==202
                    check(owned_recording,'record_start_from_standby_'+label)
                    check(wait(lambda s:s['recording'] and s['audioInputRunning'],20),'actual_pdm_recording_'+label)
                    time.sleep(8)
                    check(client.request('/api/record/stop','POST')[0]==202,'record_stop_'+label)
                    check(wait(lambda s:not s['recording'] and not s['recordRequested'],30),'record_finalized_'+label)
                    owned_recording=False;after=inventory();added=[p for p in after if p not in before and p.endswith('.avi')]
                    check(len(added)==1,'one_owned_video_'+label);path=added[0];target=args.out/(label+'.avi')
                    check(client.download('/api/media/download?path='+quote(path,safe=''),target)==after[path],'whole_video_download_'+label)
                    probe=json.loads(subprocess.run(['C:/Program Files/ffmpeg/bin/ffprobe.exe','-v','error',
                        '-show_streams','-show_format','-of','json',str(target)],capture_output=True,text=True,check=True,timeout=45).stdout)
                    media=validate(probe)
                    decoded=subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror',
                        '-i',str(target),'-f','null','-'],capture_output=True,timeout=60)
                    check(media['passed'] and media['duration_comparison_available'] and decoded.returncode==0,
                          'complete_mjpeg_pcm_decode_'+label)
                    report['clips'].append(dict(clock=clock,path=path,bytes=after[path],durations_s=media['durations_s'],
                                               duration_difference_s=media['duration_difference_s']))
                state=client.state()
                if args.sensor_standby:
                    check(not state['cameraSensorSleeping'] and state['lastWakeFrameMs']>=before_wake['uptimeSeconds']*1000,
                          'sensor_wake_and_fresh_frame_start_'+label)
                check(state['cpuMHz']==clock and state['cameraReady'] and not state['startupFailed'] and not state['captureIdle'],
                      'camera_healthy_and_selected_clock_restored_'+label)
                check(state['sdReady'] and state['configRevision']==revision and client.state('/api/wireguard')['peerSessionUp'],
                      'sd_config_wireguard_preserved_'+label)
                check(abs((time.monotonic()-state['uptimeSeconds'])-boot_at)<3,'no_reboot_'+label)
                report['cycles'].append(dict(clock=clock,mode=mode,elapsed_s=round(time.monotonic()-started,3)))
                report.setdefault('resources',[]).append(dict(clock=clock,mode=mode,
                    standby_free_heap=before_wake['freeHeap'],standby_free_psram=before_wake['freePsram'],
                    awake_free_heap=state['freeHeap'],awake_free_psram=state['freePsram']))
        final=inventory();check(all(final.get(p)==n for p,n in baseline.items()),'all_preexisting_media_preserved')
        report['passed']=True
    except Exception as error:
        report['error_type']=type(error).__name__;print('LOW-CLOCK-WAKE failed',type(error).__name__,flush=True)
    finally:
        try:
            if owned_recording:
                client.request('/api/record/stop','POST');wait(lambda s:not s['recording'],30)
            if original is not None:
                update(original,'all_original_capture_values_restored')
                check(client.state()['cpuMHz']==original['cpuMHz'],'actual_original_clock_restored')
            client.request('/api/auth/logout','POST')
        except Exception as error:report.update(passed=False,restore_error=type(error).__name__)
        client.clear();report['check_count']=len(report['checks'])
        (args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
