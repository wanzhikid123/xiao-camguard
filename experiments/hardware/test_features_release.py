"""Install/check the normal release, then restore the accepted enabled default.

The first phase keeps conversion disabled until owned hardware tests finish.
The finish phase enables historical conversion as authorized by the owner.
Never save credentials, PINs or raw network configuration.
"""
import argparse,json,time
from pathlib import Path
from appliance_client import Client
from full_mode_tools import inventory,expected_exit,decode_jpeg
ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--finish',action='store_true');args=parser.parse_args()
    c=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt')
    report={'passed':False,'checks':[]};owned=None
    def check(ok,name):
        report['checks'].append({'name':name,'passed':bool(ok)});print('RELEASE',bool(ok),name,flush=True)
        if not ok:raise RuntimeError(name)
    def health():
        until=time.monotonic()+90
        while time.monotonic()<until:
            state=c.state();wg=c.state('/api/wireguard')
            if state['cameraReady'] and state['sdReady'] and state['ble']['ready'] and wg['fullTunnelVerified']:break
            time.sleep(1)
        check(state['cameraReady'] and state['sdReady'] and state['ble']['ready'] and not state['startupFailed'],'camera_sd_ble_boot_healthy')
        check(wg['mode']=='full_tunnel' and wg['fullTunnelVerified'] and wg['publicExit'] in exits,'full_tunnel_and_server_exit_healthy')
        check(not state['timelapse']['active'] and not state['events']['armed'],'boot_stops_both_capture_modes')
        report['telemetry']={k:state[k] for k in ('freeHeap','minimumFreeHeap','freePsram','cpuMHz','temperatureC','configRevision')}
        return state
    try:
        c.login(progress=lambda p,s:print('LOGIN',p,s,flush=True));exits=expected_exit(c)
        baseline=json.loads((ROOT/'.tools/hardware/features-original-capture.json').read_text(encoding='utf8'))
        if not args.finish:
            old=inventory(c)
            check(c.request('/api/settings/transcode','POST',{'enabled':False})[0]==200,'inhibit_history_until_owned_tests_finish')
            report['firmware']=c.install(ROOT/'.tools/hardware/appliance-features-final.bin')
        else:
            check(c.request('/api/settings/transcode','POST',{'enabled':True})[0]==200,'restore_user_authorized_enabled_default')
            check(c.request('/api/device/restart','POST')[0]==202,'controlled_final_restart')
            c.wait_boot();c.login(progress=lambda p,s:print('RELOGIN',p,s,flush=True))
        if args.finish:
            # A foreground capture releases any automatically started conversion
            # before checking restored camera health; its AVI remains protected.
            code,_,body=c.request('/api/camera/photo','POST',timeout=30)
            check(code==201,'foreground_camera_available_after_enabled_reboot')
            owned=json.loads(body)['path']
            check(c.request('/api/media/delete','POST',{'path':owned})[0]==200,'owned_final_photo_removed');owned=None
        health()
        check(c.state('/api/settings/capture')['values']==baseline,'all_original_capture_settings_preserved')
        for name in ('index.html','app.js','i18n.js','style.css'):
            check(c.request('/' if name=='index.html' else '/'+name,authorized=False)[2]==(ROOT/'ui/appliance'/name).read_bytes(),'normal_installed_asset_'+name)
        for path,method in (('/api/codecs/test','GET'),('/api/codecs/file','GET'),('/api/timelapse/test-index','POST'),('/api/events/test','POST')):
            check(c.request(path,method)[0]==404,'fixture_excluded_'+path)
        if args.finish:
            check(c.state('/api/settings/transcode')['enabled'],'enabled_setting_persists_across_reboot')
        else:
            check(not c.state('/api/settings/transcode')['enabled'],'disabled_setting_persists_through_ota')
            code,_,body=c.request('/api/camera/photo','POST',timeout=30)
            check(code==201,'normal_owned_3mp_photo')
            from urllib.parse import quote
            owned=json.loads(body)['path'];image=ROOT/'.tools/hardware/features-normal-photo.jpg'
            c.download('/api/media/download?path='+quote(owned,safe=''),image)
            check(decode_jpeg(image.read_bytes(),image),'normal_jpeg_full_decode')
            check(c.request('/api/media/delete','POST',{'path':owned})[0]==200,'owned_normal_photo_removed');owned=None
            check(inventory(c)==old,'all_preexisting_media_paths_and_sizes_preserved_through_normal_ota')
        report['passed']=True
    finally:
        if owned:c.request('/api/media/delete','POST',{'path':owned})
        c.clear();(ROOT/('.tools/hardware/features-release-final.json' if args.finish else '.tools/hardware/features-release-install.json')).write_text(json.dumps(report,indent=2),encoding='utf8')
if __name__=='__main__':main()
