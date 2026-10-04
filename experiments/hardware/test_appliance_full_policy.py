"""Production full-tunnel guard/lifecycle fixture; restore a normal image.

The only compiled test inputs are synthetic packets/clock and a documentation
endpoint. Real keys stay in NVS. A verified server DNS alias is supplied in RAM.
"""
import argparse
import json
from pathlib import Path
import socket
import time
from appliance_client import Client
from full_mode_tools import ROOT,expected_exit,decode_jpeg,inventory


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address',required=True);parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--restore',type=Path,required=True)
    parser.add_argument('--reset-before',action='store_true')
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/full-policy-fixture')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    client=Client(args.address,ROOT/'.tools/private/admin-password.txt');expected=expected_exit(client)
    alias=(ROOT/'.tools/hardware/wg-endpoint-alias.txt').read_text(encoding='utf8').strip()
    if socket.gethostbyname(alias) not in expected:raise RuntimeError('Server alias did not match')
    report=dict(passed=False,checks=[],credentials_or_tokens_saved=False)
    installed=False
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)));print('FULL-POLICY','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('Policy fixture assertion: '+name)
    def wait(predicate,path='/api/wireguard',seconds=90,offline=False):
        deadline=time.monotonic()+seconds;state={}
        while time.monotonic()<deadline:
            try:
                state=client.state(path)
                if predicate(state):return state
            except OSError:
                if not offline:raise
            time.sleep(.3)
        report['wait_failure']={'path':path,'last_state':state,'wg':client.state('/api/wireguard'),'policy':client.state('/api/wireguard/policy-test')}
        raise RuntimeError('Policy fixture wait deadline')
    def action(value,**fields):
        check(client.request('/api/wireguard/policy-test','POST',{'action':value,**fields})[0]==202,'queued_'+value+'_fixture')
    def full_proof(name):
        state=wait(lambda state:state['fullTunnelVerified'])
        check(state['guardReady'] and state['publicExit'] in expected,name)
    def mode(value):
        check(client.request('/api/wireguard/mode','POST',{'mode':value})[0]==202,'queued_'+value+'_trial')
        return wait(lambda state:state['state']=='awaiting_local_confirmation','/api/network',65)
    def confirm(state):
        check(client.request('/api/network/confirm','POST',{'revision':state['revision']})[0]==202,'local_confirmation_accepted')
        wait(lambda state:not state['revision'] and state['state']=='ready','/api/network')
    def jpeg(name):
        code,_,body=client.request('/api/camera/still',timeout=30)
        check(code==200 and decode_jpeg(body,args.out/(name+'.jpg')),name+'_full_jpeg_decode')
    try:
        report['stage']='initial_login';client.login();check(True,'chosen_account_login')
        if args.reset_before:
            check(not client.state()['recording'],'reset_only_while_idle')
            check(client.request('/api/device/restart','POST')[0]==202,'clear_prior_candidate_heap_with_normal_reset')
            client.wait_boot();client.login()
        report['stage']='snapshot';before=client.state('/api/settings/capture')['values'];old_media=inventory(client)
        report['stage']='preparation_ota';print('FULL-POLICY preparation OTA',flush=True)
        installed=True;report['fixture_preparation_installation']=client.install(args.fixture)
        if client.state('/api/wireguard')['mode']!='full_tunnel':confirm(mode('full_tunnel'))
        revision=client.state()['configRevision'];report['stage']='cold_boot_ota';report['fixture_installation']=client.install(args.fixture)
        full_proof('cold_clock_boot_has_actual_server_exit')
        initial=client.state('/api/wireguard/policy-test');report['cold_boot_observation']=initial
        check(initial['coldClockReset'] and initial['bootstrapTimePackets']>0,'real_boot_from_unset_clock_uses_bounded_numeric_ntp')
        check(initial['requested'] and initial['ready'] and initial['guardedInterfaces']>=1,'global_physical_callbacks_guarded_after_boot')
        check(initial['dnsServers'][1:]==['0.0.0.0','0.0.0.0'],'no_secondary_dhcp_resolver_in_full_mode')
        check(client.request('/api/wireguard/policy-test',authorized=False)[0]==401,'fixture_get_requires_session')
        check(client.request('/api/wireguard/policy-test','POST',{'action':'packets'},headers={'X-CSRF-Token':''})[0]==403,'fixture_requires_csrf')
        check(client.request('/api/wireguard/policy-test?extra=1','POST',{'action':'packets'})[0]==400,'fixture_rejects_query')
        check(client.request('/api/wireguard/policy-test','POST',{'action':'packets','extra':1})[0]==400,'fixture_rejects_unknown_fields')
        action('packets');packets=wait(lambda state:state['packetChecks']>0,'/api/wireguard/policy-test')
        report['packet_observation']=packets
        check(packets['packetChecks']>=70 and packets['packetFailures']==0,'actual_global_output_callback_packet_matrix')
        check(packets['ipv4Blocked']>15 and packets['ipv6Blocked']>=13,'dns_fragment_ipv6_invalid_interface_and_boot_fail_closed')
        action('hold');down=wait(lambda state:not state['tunnelPresent'] and state['outboundDone'],'/api/wireguard/policy-test',25)
        report['absent_tunnel_observation']=down
        check(down['requested'] and down['ready'] and down['outboundHTTP']!=200 and down['outboundAddress']=='0.0.0.0','real_unbound_https_blocked_with_absent_tunnel')
        check(down['ipv4Blocked']>packets['ipv4Blocked'],'real_physical_fallback_attempts_are_blocked')
        check(not client.state('/api/wireguard')['fullTunnelVerified'],'absent_tunnel_cannot_claim_verified')
        jpeg('tunnel-down-lan');full_proof('full_routes_and_proof_recover_after_hold')
        action('bad_endpoint');wait(lambda state:not state['peerSessionUp'])
        check(not client.state('/api/wireguard')['fullTunnelVerified'],'unavailable_endpoint_revokes_proof')
        jpeg('bad-endpoint-lan');full_proof('real_endpoint_routes_recover_after_address_injection')
        action('bootstrap',hostname=alias)
        wait(lambda state:not state['fullTunnelVerified'])
        full_proof('real_hostname_endpoint_dns_and_unset_clock_recover')
        bootstrap=client.state('/api/wireguard/policy-test');report['hostname_bootstrap_observation']=bootstrap
        check(bootstrap['bootstrapDNSPackets']>packets['bootstrapDNSPackets'] and bootstrap['bootstrapTimePackets']>packets['bootstrapTimePackets'],
              'actual_hostname_a_query_and_numeric_ntp_use_bounded_leases')
        check(not bootstrap['endpointDNSLease'] and not bootstrap['timeLease'] and bootstrap['dnsServers']==initial['dnsServers'],
              'bootstrap_leases_close_and_explicit_tunnel_dns_returns')
        action('restore');full_proof('original_endpoint_restored_in_ram')
        action('wifi');time.sleep(2)
        full_proof_state=wait(lambda state:state['fullTunnelVerified'],offline=True)
        check(full_proof_state['guardReady'] and full_proof_state['publicExit'] in expected,'actual_wifi_disconnect_dhcp_reconnect_restores_guard_and_exit')
        check(client.state('/api/wireguard/policy-test')['dnsServers']==initial['dnsServers'],'dhcp_reconnect_cannot_restore_router_dns_fallback')
        network=mode('remote_access');check(client.request('/api/network/cancel','POST')[0]==202,'cancel_remote_trial')
        wait(lambda state:not state['revision'] and state['state']=='ready','/api/network')
        full_proof('remote_trial_cancellation_restores_full_proof')
        network=mode('remote_access');check(client.request('/api/device/restart','POST')[0]==202,'interrupt_remote_trial_with_reset')
        client.wait_boot();client.login();full_proof('reset_discards_unconfirmed_remote_mode')
        check(client.state()['configRevision']==revision,'faults_and_interrupted_trial_never_promote_config')
        check(client.state('/api/settings/capture')['values']==before,'all_capture_settings_survive_faults')
        current_media=inventory(client)
        check(all(current_media.get(path)==size for path,size in old_media.items()),'all_existing_media_preserved')
        report['original_media_count']=len(old_media);report['passed']=True
    except Exception as error:
        report['passed']=False;report['error_type']=type(error).__name__;print('FULL-POLICY FAIL',type(error).__name__,flush=True)
    finally:
        try:
            if installed:
                if not client.cookie:client.login()
                if client.state('/api/network')['revision']:
                    client.request('/api/network/cancel','POST');wait(lambda state:not state['revision'],'/api/network')
                if client.request('/api/wireguard/policy-test')[0]==200 and client.state('/api/wireguard')['mode']=='full_tunnel':action('restore')
                if client.state('/api/wireguard')['mode']=='full_tunnel':confirm(mode('remote_access'))
                report['normal_restoration']=client.install(args.restore)
                check(client.request('/api/wireguard/policy-test')[0]==404,'normal_image_has_no_policy_fixture')
                check(client.state()['cpuMHz']==240 and client.state()['cameraReady'] and client.state()['sdReady'],'restored_stock_clock_camera_sd')
                check(client.state('/api/wireguard')['peerSessionUp'],'restored_remote_peer')
            if client.cookie:client.request('/api/auth/logout','POST')
        except Exception as error:
            report['passed']=False;report['cleanup_error_type']=type(error).__name__;print('FULL-POLICY cleanup FAIL',type(error).__name__,flush=True)
        client.clear();report['check_count']=len(report['checks']);(args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
