"""Normal-firmware physical missing-card/reinsertion acceptance.

Create a public-only baseline with the inserted card first. The user must power
off before removing/reinserting it. No fixture/image/format/delete is performed.
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
    parser.add_argument('--stage',choices=('baseline','absent','restored'),required=True)
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/physical-card')
    parser.add_argument('--serial-log',type=Path,help='Same-run serial capture; assert the missing-config motion regression is absent')
    parser.add_argument('--exercise-storage',action='store_true',help='Restored stage only: save/download/decode one owned photo and audio AVI')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    reference=args.out/'baseline.json';report=dict(passed=False,checks=[],credentials_or_tokens_saved=False)
    client=Client(args.address,ROOT/'.tools/private/admin-password.txt');owned_recording=False
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)))
        print('PHYSICAL-SD','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('Physical card assertion: '+name)
    def inventory():
        items={};cursor='';dates=[]
        for _ in range(32):
            data=client.state('/api/media/dates'+('?before='+quote(cursor,safe='') if cursor else ''))
            if data.get('enumerationTruncated'):raise RuntimeError('Incomplete date enumeration')
            dates.extend(item['date'] for item in data['items'])
            if not data['more']:break
            cursor=data['nextBefore']
        else:raise RuntimeError('Date enumeration bound')
        for date in dates:
            cursor=''
            for _ in range(128):
                data=client.state('/api/media?date='+date+('&before='+quote(cursor,safe='') if cursor else ''))
                if data.get('enumerationTruncated'):raise RuntimeError('Incomplete file enumeration')
                items.update({item['path']:item['bytes'] for item in data['items']})
                if not data['more']:break
                cursor=data['nextBefore']
            else:raise RuntimeError('File enumeration bound')
        return items
    def jpeg():
        code,_,body=client.request('/api/camera/still',timeout=30)
        check(code==200 and body.startswith(b'\xff\xd8') and body.endswith(b'\xff\xd9'),'normal_still_available')
        target=args.out/(args.stage+'-still-'+str(len(report['checks']))+'.jpg');target.write_bytes(body)
        decoded=subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror',
            '-i',str(target),'-f','null','-'],capture_output=True,timeout=30)
        check(decoded.returncode==0,'complete_normal_jpeg_decode')
    try:
        client.login();check(True,'chosen_account_login_on_normal_image')
        state=client.state();values=client.state('/api/settings/capture')['values']
        check(not state['recording'] and not state['recordRequested'],'no_active_recording')
        check(state['cameraReady'] and not state['startupFailed'],'camera_healthy')
        check(client.state('/api/wireguard')['peerSessionUp'],'real_wireguard_session')
        for name in ('index.html','app.js','i18n.js','style.css'):
            route='/' if name=='index.html' else '/'+name
            check(client.request(route,authorized=False)[2]==(ROOT/'ui/appliance'/name).read_bytes(),
                  'embedded_asset_matches_'+name)
        if args.stage=='baseline':
            check(state['sdReady'],'card_mounted_before_physical_test')
            report.update(values=values,media=inventory(),config_revision=state['configRevision'])
            jpeg();report['passed']=True
        else:
            baseline=json.loads(reference.read_text(encoding='utf8'))
            check(baseline.get('passed'),'public_baseline_available')
            check(values==baseline['values'] and state['configRevision']==baseline['config_revision'],
                  'all_capture_values_and_network_revision_preserved')
            if args.stage=='absent':
                check(not state['sdReady'],'actual_card_not_mounted')
                for path,method in (('/api/media/dates','GET'),('/api/media?date=20261003','GET'),
                                    ('/api/camera/photo','POST'),('/api/record/start','POST')):
                    check(client.request(path,method)[0]==503,'absent_card_refused_'+path)
                jpeg()
                check(client.request('/api/device/restart','POST')[0]==202,'normal_absent_card_restart')
                client.wait_boot();client.login();state=client.state()
                check(not state['sdReady'] and state['cameraReady'] and not state['startupFailed'],
                      'normal_absent_card_restart_recovery')
                check(client.state('/api/settings/capture')['values']==values,'absent_restart_preserves_values')
                check(client.state('/api/wireguard')['peerSessionUp'],'wireguard_after_absent_restart')
                jpeg()
            else:
                check(state['sdReady'],'physical_card_mounted_again')
                current=inventory()
                check(all(current.get(path)==size for path,size in baseline['media'].items()),
                      'all_preexisting_media_paths_and_sizes_preserved')
                jpeg()
                if args.exercise_storage:
                    code,_,body=client.request('/api/camera/photo','POST',timeout=30)
                    check(code==201,'restored_card_accepts_photo_write');photo=json.loads(body)
                    check(photo['path'] not in current,'restored_photo_does_not_overwrite_existing_file')
                    target=args.out/'restored-owned-photo.jpg'
                    check(client.download('/api/media/download?path='+quote(photo['path'],safe=''),target)==photo['bytes'],'restored_whole_photo_readback')
                    decoded=subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror','-i',str(target),'-f','null','-'],capture_output=True,timeout=30)
                    check(decoded.returncode==0,'restored_photo_full_decode')
                    before=inventory();owned_recording=client.request('/api/record/start','POST')[0]==202
                    check(owned_recording,'restored_card_accepts_manual_recording')
                    until=time.monotonic()+20
                    while time.monotonic()<until and not client.state()['recording']:time.sleep(.3)
                    check(client.state()['recording'] and client.state()['audioInputRunning'],'restored_real_audio_recording')
                    time.sleep(8);check(client.request('/api/record/stop','POST')[0]==202,'restored_record_stop')
                    until=time.monotonic()+30
                    while time.monotonic()<until and client.state()['recording']:time.sleep(.3)
                    check(not client.state()['recording'] and not client.state()['recordRequested'],'restored_record_finalized');owned_recording=False
                    after=inventory();added=[path for path in after if path not in before and path.endswith('.avi')]
                    check(len(added)==1,'restored_one_owned_video');target=args.out/'restored-owned-video.avi'
                    check(client.download('/api/media/download?path='+quote(added[0],safe=''),target)==after[added[0]],'restored_whole_video_readback')
                    probe=json.loads(subprocess.run(['C:/Program Files/ffmpeg/bin/ffprobe.exe','-v','error','-show_streams','-show_format','-of','json',str(target)],capture_output=True,text=True,check=True,timeout=30).stdout)
                    media=validate(probe);decoded=subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror','-i',str(target),'-f','null','-'],capture_output=True,timeout=45)
                    check(media['passed'] and media['duration_comparison_available'] and decoded.returncode==0,'restored_full_mjpeg_pcm_decode')
                    check(all(after.get(path)==size for path,size in baseline['media'].items()),'storage_exercise_preserves_all_preexisting_media')
                    check(client.state('/api/settings/capture')['values']==values and client.state()['configRevision']==baseline['config_revision'],'storage_exercise_preserves_values_and_revision')
                    report['restored_clip']=dict(path=added[0],bytes=target.stat().st_size,durations_s=media['durations_s'],difference_s=media['duration_difference_s'])
            report['passed']=True
        if args.serial_log:
            serial=args.serial_log.read_text(encoding='utf8')
            count=serial.count('motionDetect: image dimensions are too large')
            report['unexpected_motion_errors']=count
            check(count==0,'no_motion_processing_from_missing_sd_defaults')
    except Exception as error:
        report['passed']=False
        report['error']=type(error).__name__+': '+str(error);print(report['error'],flush=True)
    finally:
        if owned_recording:
            try:client.request('/api/record/stop','POST');time.sleep(3)
            except Exception:pass
        if client.cookie:
            try:client.request('/api/auth/logout','POST')
            except Exception:pass
        client.clear();report['check_count']=len(report['checks'])
        target=reference if args.stage=='baseline' else args.out/(args.stage+'-report.json')
        target.write_text(json.dumps(report,indent=2),encoding='utf8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
