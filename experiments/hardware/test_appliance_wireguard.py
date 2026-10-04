"""Product WG-A import/session/routing/recovery gate with local private config.

Leaves the confirmed remote-access configuration active. Does not enable the
PC VPN, full tunnel or mutate the server. Remote UI acceptance is separate.
"""
import argparse
import json
from pathlib import Path
import re
import time
from urllib.parse import quote
from appliance_client import Client

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address',required=True)
    parser.add_argument('--password-file',type=Path,default=ROOT/'.tools/private/admin-password.txt')
    parser.add_argument('--config',type=Path,default=ROOT/'XIAO_ESP32_Camera.conf')
    parser.add_argument('--firmware',type=Path,default=ROOT/'.pio/build/xiao-appliance/firmware.bin')
    parser.add_argument('--install-first',action='store_true')
    parser.add_argument('--probe-address',default='10.8.0.1')
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/appliance-wireguard')
    args=parser.parse_args(); args.out.mkdir(parents=True,exist_ok=True)
    client=Client(args.address,args.password_file)
    config=args.config.read_text(encoding='utf-8-sig')
    for name in ('PrivateKey','PublicKey','PresharedKey'):
        value=re.search(r'^\s*'+name+r'\s*=\s*([^\r\n]+)',config,re.M)
        if value: client.private_values.append(value.group(1).strip().encode())
    report=dict(passed=False,checks=[],credentials_or_tokens_saved=False,remote_access_verified=False,full_tunnel_verified=False)

    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok))); print(f"WG-A {'PASS' if ok else 'FAIL'} {name}",flush=True)
        if not ok: raise RuntimeError('WG-A check failed: '+name)

    def wait(predicate,path='/api/network',seconds=95):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            status=client.state(path)
            if predicate(status): return status
            time.sleep(.3)
        raise RuntimeError('WG-A state wait timed out')

    try:
        client.login(); check(True,'chosen_account_login')
        if args.install_first:
            report['firmware']=client.install(args.firmware); check(True,'new_wireguard_receiver_installed_over_authenticated_ota')
        check(client.state()['cameraReady'] and client.state()['sdReady'],'camera_sd_available_before_import')
        check(client.request('/api/wireguard',authorized=False)[0]==401,'wg_status_requires_session')
        check(client.request('/api/wireguard/import','POST',raw=b'x',authorized=False,headers={'Content-Type':'text/plain'})[0]==403,'wg_import_requires_session')
        check(client.request('/api/wireguard/import','POST',raw=b'x',headers={'Content-Type':'text/plain','X-CSRF-Token':''})[0]==403,'wg_import_requires_csrf')
        check(client.request('/api/wireguard/import?extra=1','POST',raw=b'x',headers={'Content-Type':'text/plain'})[0]==400,'wg_import_rejects_query')
        check(client.request('/api/wireguard/import','POST',raw=b'\0',headers={'Content-Type':'text/plain'})[0]==400,'wg_import_rejects_nul')
        check(client.request('/api/wireguard/import','POST',raw=b'x'*4097,headers={'Content-Type':'text/plain'})[0]==400,'wg_import_bounds_text_size')
        for path in ('/api/wireguard/export','/api/wireguard/config'):
            check(client.request(path)[0]==404,'no_raw_config_export')
        old_revision=client.state()['configRevision']
        check(client.request('/api/wireguard/import','POST',raw=b'[Interface]\nPostUp = forbidden\n',headers={'Content-Type':'text/plain'})[0]==202,'invalid_config_queued_for_owner_validation')
        wait(lambda state:state['importError'] not in ('none','ok'),'/api/wireguard',seconds=5)
        check(client.state()['configRevision']==old_revision and not client.state('/api/network')['revision'],'bad_import_does_not_persist_or_replace_active')
        check(client.request('/api/wireguard/import','POST',raw=config.encode(),headers={'Content-Type':'text/plain; charset=utf-8'})[0]==202,'private_single_peer_import_accepted')
        network=wait(lambda state:state['state']=='awaiting_local_confirmation')
        wg=client.state('/api/wireguard')
        check(wg['peerSessionUp'] and wg['running'],'actual_authenticated_peer_session')
        check(wg['mode']=='remote_access' and not wg['fullTunnelVerified'],'full_tunnel_not_claimed_by_session')
        check(wg['importedDefaultRouteExcluded'] and wg['excludedIPv6Entries']>=1,'default_and_ipv6_exclusions_reported')
        check(client.state()['cameraReady'] and client.state()['wifiConnected'],'lan_camera_remain_usable_during_wg_trial')
        check(client.request('/api/network/confirm','POST',{'revision':network['revision']})[0]==202,'actual_lan_confirmation_accepted')
        active=wait(lambda state:state['state']=='ready' and state['revision']==0)
        check(active['activeRevision']>old_revision and not active['rescueAP'],'wg_trial_promoted_and_recovery_ap_closed')
        check(client.state()['wireguardConfigured'] and client.state()['wireguardRunning'],'active_configuration_and_running_interface_distinguished')
        before=client.state('/api/wireguard')
        check(client.request('/api/wireguard/probe','POST',{'address':'192.168.1.1'})[0]==400,'probe_cannot_select_physical_lan')
        check(client.request('/api/wireguard/probe','POST',{'address':args.probe_address})[0]==202,'bounded_unbound_destination_probe_accepted')
        probe=wait(lambda state:state['probeAddress']==args.probe_address and not state['probeBusy'] and state['probeSent']==3,'/api/wireguard',seconds=8)
        report['routing_probe']=dict(target=probe['probeAddress'],sent=probe['probeSent'],received=probe['probeReceived'],
                                    authenticated_tx_delta=probe['sentBytes']-before['sentBytes'],authenticated_rx_delta=probe['receivedBytes']-before['receivedBytes'])
        check(probe['probeReceived']>0 and probe['sentBytes']>before['sentBytes'] and probe['receivedBytes']>before['receivedBytes'],'selected_wg_route_has_authenticated_echo_replies')
        check(client.request('/api/record/start','POST')[0]==202,'manual_audio_recording_with_wg_accepted')
        wait(lambda state:state['recording'],path='/api/status',seconds=5); time.sleep(12)
        check(client.state('/api/wireguard')['peerSessionUp'],'wg_session_survives_camera_audio_recording')
        check(client.request('/api/record/stop','POST')[0]==202,'recording_stops_with_wg')
        wait(lambda state:not state['recording'] and not state['recordRequested'],path='/api/status',seconds=8)
        code,_,photo=client.request('/api/camera/still')
        check(code==200 and photo.startswith(b'\xff\xd8') and photo.endswith(b'\xff\xd9'),'jpeg_remains_available_with_wg')
        (args.out/'wg-active-still.jpg').write_bytes(photo)
        check(client.request('/api/device/restart','POST')[0]==202,'normal_restart_with_wg_configuration')
        client.wait_boot(); client.login(); check(True,'chosen_account_login_after_wg_restart')
        wait(lambda state:state['state']=='ready' and state['revision']==0)
        check(client.state('/api/wireguard')['peerSessionUp'] and client.state()['wireguardConfigured'],'confirmed_wg_configuration_and_peer_recover_after_reset')
        check(client.state()['cameraReady'] and client.state()['sdReady'],'camera_sd_recover_after_wg_reset')
        report['final_masked_wg_status']=client.state('/api/wireguard')
        report['maximum_http_transaction_s']=round(client.maximum_latency,3)
        check(client.request('/api/auth/logout','POST')[0]==200,'final_logout')
        report['passed']=True
    except Exception as error:
        report['error']=type(error).__name__+': '+str(error); print(report['error'],flush=True)
    finally:
        config=''; client.clear(); report['check_count']=len(report['checks'])
        (args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__=='__main__': raise SystemExit(main())
