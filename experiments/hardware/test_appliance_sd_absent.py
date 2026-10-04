"""Real-board missing-mount fixture gate, always restoring the normal image.

The explicit fixture skips SD initialization; it does not format, write, delete
or access the inserted card. This is not proof of physical removal/contact faults.
"""
import argparse
import json
from pathlib import Path
import subprocess
import time
from urllib.parse import quote
from appliance_client import Client

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address',required=True)
    parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--restore',type=Path,required=True)
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/appliance-sd-absent')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    # Check restoration material before changing the running firmware.
    if not args.fixture.is_file() or not args.restore.is_file():raise RuntimeError('Both firmware images are required')
    client=Client(args.address,ROOT/'.tools/private/admin-password.txt')
    report=dict(passed=False,checks=[],physical_card_removed=False,credentials_or_tokens_saved=False)
    installed=False;baseline=None;values=None
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)))
        print('SD-ABSENT','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('SD fixture assertion: '+name)
    def files():
        date=time.strftime('%Y%m%d',time.gmtime());cursor='';items={}
        for _ in range(32):
            data=client.state('/api/media?date='+date+('&before='+quote(cursor,safe='') if cursor else ''))
            items.update({item['path']:item['bytes'] for item in data['items']})
            if not data['more']:return items
            cursor=data['nextBefore']
        raise RuntimeError('Enumeration bound')
    def fixture_state(label):
        state=client.state();check(state['cameraReady'] and not state['startupFailed'],'camera_healthy_'+label)
        check(not state['sdReady'] and state['sdFreeBytes']==0,'mount_absent_'+label)
        check(client.state('/api/wireguard')['peerSessionUp'],'wg_usable_'+label)
        check(client.state('/api/settings/capture')['values']==values,'capture_values_preserved_'+label)
    try:
        client.login();state=client.state()
        check(not state['recording'] and not state['recordRequested'] and state['sdReady'],'normal_card_ready_no_active_recording')
        baseline=files();values=client.state('/api/settings/capture')['values']
        # Even an interrupted install attempt is followed by the restore path.
        installed=True;report['fixture']=client.install(args.fixture);check(True,'fixture_installed_over_wg')
        fixture_state('initial')
        for route,file in [('/','index.html'),('/app.js','app.js'),('/i18n.js','i18n.js'),('/style.css','style.css')]:
            code,_,body=client.request(route,authorized=False)
            check(code==200 and body==(ROOT/'ui/appliance'/file).read_bytes(),'embedded_asset_without_sd_'+file)
        for path,method in [('/api/media/dates','GET'),('/api/media?date='+time.strftime('%Y%m%d',time.gmtime()),'GET'),('/api/camera/photo','POST'),('/api/record/start','POST')]:
            check(client.request(path,method)[0]==503,'missing_sd_refusal_'+path.split('?')[0])
        code,_,body=client.request('/api/camera/still',timeout=30)
        check(code==200 and body[:2]==b'\xff\xd8','still_camera_without_sd')
        file=args.out/'without-sd.jpg';file.write_bytes(body)
        decoded=subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror','-i',str(file),'-f','null','-'],capture_output=True,timeout=30)
        check(decoded.returncode==0,'complete_jpeg_decode_without_sd')
        check(client.request('/api/device/restart','POST')[0]==202,'restart_without_sd')
        client.wait_boot();client.login();fixture_state('after_reset')
        report['passed']=True
    except Exception as error:
        report['error']=type(error).__name__+': '+str(error);print(report['error'],flush=True)
    finally:
        if installed:
            try:
                report['restore']=client.install(args.restore)
                check(client.state()['sdReady'] and client.state()['cameraReady'] and client.state('/api/wireguard')['peerSessionUp'],'normal_firmware_camera_card_wg_restored')
                check(files()==baseline,'every_preexisting_media_path_and_size_preserved')
                check(client.state('/api/settings/capture')['values']==values,'normal_capture_settings_restored')
            except Exception as error:
                report.update(passed=False,restore_error=type(error).__name__+': '+str(error));print('SD-ABSENT restore requires follow-up',flush=True)
        try:client.request('/api/auth/logout','POST')
        except Exception:pass
        client.clear();report['check_count']=len(report['checks']);(args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
