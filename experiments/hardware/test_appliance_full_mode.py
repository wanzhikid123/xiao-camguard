"""Production WG-B local trial, real public verification, cancellation/reset gate.

Uses local ignored credentials in RAM. Starts and ends in remote-access mode;
does not replace WG keys, Wi-Fi, admin account or capture preferences.
"""
import argparse
import ipaddress
import json
from pathlib import Path
import re
import socket
import time
from appliance_client import Client

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address',required=True)
    parser.add_argument('--install',type=Path)
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/full-mode-candidate')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    client=Client(args.address,ROOT/'.tools/private/admin-password.txt')
    report=dict(passed=False,checks=[],credentials_or_tokens_saved=False)
    config=(ROOT/'XIAO_ESP32_Camera.conf').read_text(encoding='utf-8-sig')
    endpoint=re.search(r'^\s*Endpoint\s*=\s*([^\r\n]+)',config,re.M)
    if not endpoint:raise RuntimeError('Missing configured endpoint')
    expected_exit={str(ipaddress.IPv4Address(item[4][0])) for item in socket.getaddrinfo(
        endpoint.group(1).strip().rsplit(':',1)[0],None,socket.AF_INET,socket.SOCK_DGRAM)}
    for name in ('PrivateKey','PublicKey','PresharedKey'):
        secret=re.search(r'^\s*'+name+r'\s*=\s*(\S+)',config,re.M)
        if secret:client.private_values.append(secret.group(1).encode())
    config=''
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)));print('FULL-MODE','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('Full mode assertion: '+name)
    def wait(predicate,path='/api/network',seconds=65):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            state=client.state(path)
            if predicate(state):return state
            time.sleep(.3)
        report['wait_failure']={'path':path,'last_state':state,'wireguard':client.state('/api/wireguard')}
        raise RuntimeError('Full mode wait deadline')
    def mode(value):
        check(client.request('/api/wireguard/mode','POST',{'mode':value})[0]==202,'queued_'+value+'_trial')
        return wait(lambda state:state['state']=='awaiting_local_confirmation')
    def confirm(network):
        check(client.request('/api/network/confirm','POST',{'revision':network['revision']})[0]==202,'local_confirmation_accepted')
        return wait(lambda state:state['state']=='ready' and not state['revision'])
    try:
        client.login(progress=lambda phase,s:print('FULL-MODE login',phase,s,flush=True))
        before=client.state();values=client.state('/api/settings/capture')['values']
        check(before['cameraReady'] and before['sdReady'] and not before['recording'],'normal_camera_sd_idle')
        if args.install:
            report['installation']=client.install(args.install);check(True,'authenticated_candidate_ota')
        check(client.state('/api/wireguard')['mode']=='remote_access','initial_remote_mode')
        check(client.request('/api/wireguard/mode','POST',{'mode':'full_tunnel'},authorized=False)[0]==403,'mode_requires_session')
        check(client.request('/api/wireguard/mode','POST',{'mode':'full_tunnel'},headers={'X-CSRF-Token':''})[0]==403,'mode_requires_csrf')
        check(client.request('/api/wireguard/mode?extra=1','POST',{'mode':'full_tunnel'})[0]==400,'mode_rejects_query')
        for body in ({},{'mode':'unknown'},{'mode':'full_tunnel','extra':1},{'mode':True},{'mode':{'nested':'full_tunnel'}}):
            check(client.request('/api/wireguard/mode','POST',body)[0]==400,'mode_rejects_invalid_schema')
        check(client.request('/api/wireguard/mode','POST',{'mode':'remote_access'})[0]==409,'mode_rejects_duplicate')
        original_revision=client.state()['configRevision']
        network=mode('full_tunnel');full=client.state('/api/wireguard');report['first_full_observation']=full
        check(full['mode']=='full_tunnel' and full['guardReady'] and full['fullTunnelVerified'],'real_full_route_guard_and_public_verification')
        check(full['publicExit'] in expected_exit and full['sentBytes']>0 and full['receivedBytes']>0,'expected_server_exit_and_real_peer_data')
        check(client.state()['cameraReady'] and client.state()['sdReady'],'camera_sd_healthy_during_full_trial')
        check(client.request('/api/network/cancel','POST')[0]==202,'full_trial_cancel_accepted')
        wait(lambda state:not state['revision'] and state['state']=='ready')
        check(client.state('/api/wireguard')['mode']=='remote_access' and client.state()['configRevision']==original_revision,
              'cancel_restores_selected_remote_config')
        network=mode('full_tunnel');selected=confirm(network)
        check(selected['activeRevision']>original_revision,'full_mode_promoted_after_real_local_and_outbound_proof')
        full_revision=selected['activeRevision']
        check(client.request('/api/device/restart','POST')[0]==202,'normal_full_mode_restart')
        client.wait_boot();client.login()
        wait(lambda state:state['fullTunnelVerified'],'/api/wireguard')
        check(client.state()['configRevision']==full_revision,'full_mode_persists_after_normal_reset')
        check(client.state()['cameraReady'] and client.state()['sdReady'],'full_reset_camera_sd_healthy')
        check(client.request('/api/wireguard/restart','POST')[0]==202,'owner_restarts_full_peer')
        wait(lambda state:not state['fullTunnelVerified'],'/api/wireguard',10)
        down=client.state('/api/wireguard')
        check(down['mode']=='full_tunnel' and not down['fullTunnelVerified'],'peer_restart_invalidates_verification')
        wait(lambda state:state['fullTunnelVerified'],'/api/wireguard')
        check(True,'full_route_and_public_proof_recover_after_peer_restart')
        network=mode('remote_access')
        check(client.request('/api/network/cancel','POST')[0]==202,'remote_trial_cancel_accepted')
        wait(lambda state:not state['revision'] and state['state']=='ready')
        check(client.state('/api/wireguard')['fullTunnelVerified'] and client.state()['configRevision']==full_revision,
              'cancelled_remote_trial_restores_full_guard_and_real_proof')
        network=mode('remote_access');confirm(network)
        check(client.state('/api/wireguard')['mode']=='remote_access','explicit_confirmed_remote_mode_restored')
        check(client.state('/api/settings/capture')['values']==values and client.state()['cpuMHz']==240,'all_capture_values_and_clock_preserved')
        check(client.state()['cameraReady'] and client.state()['sdReady'] and client.state('/api/wireguard')['peerSessionUp'],
              'final_camera_sd_peer_healthy')
        report['passed']=True
    except Exception as error:
        report['passed']=False;report['error_type']=type(error).__name__
        print('FULL-MODE FAIL',type(error).__name__,flush=True)
    finally:
        try:
            if client.cookie:
                network=client.state('/api/network')
                if network['revision']:
                    client.request('/api/network/cancel','POST');wait(lambda state:not state['revision'] and state['state']=='ready')
                if client.state('/api/wireguard')['mode']=='full_tunnel':confirm(mode('remote_access'))
                client.request('/api/auth/logout','POST')
        except Exception as error:
            report['passed']=False;report['cleanup_error_type']=type(error).__name__
            print('FULL-MODE cleanup FAIL',type(error).__name__,flush=True)
        client.clear();report['check_count']=len(report['checks'])
        (args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
