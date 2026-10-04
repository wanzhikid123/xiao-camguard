"""Actual WG-A bad-DNS/PSK/reset rollback and confirmed-clear recovery gate."""
import argparse
import base64
import json
from pathlib import Path
import re
import secrets
import subprocess
import sys
import time
from urllib.parse import quote
from appliance_client import Client

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts'))
from validate_avi import validate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True)
    parser.add_argument('--password-file', type=Path, default=ROOT/'.tools/private/admin-password.txt')
    parser.add_argument('--config', type=Path, default=ROOT/'XIAO_ESP32_Camera.conf')
    parser.add_argument('--out', type=Path, default=ROOT/'.tools/hardware/appliance-wg-failure')
    parser.add_argument('--install-first', action='store_true')
    parser.add_argument('--firmware', type=Path, default=ROOT/'.pio/build/xiao-appliance/firmware.bin')
    args = parser.parse_args(); args.out.mkdir(parents=True, exist_ok=True)
    client = Client(args.address, args.password_file); config = args.config.read_text(encoding='utf-8-sig')
    for name in ('PrivateKey','PublicKey','PresharedKey'):
        value = re.search(r'^\s*'+name+r'\s*=\s*([^\r\n]+)', config, re.M)
        if value: client.private_values.append(value.group(1).strip().encode())
    report = dict(passed=False, checks=[], status_latencies_s=[], media=[], credentials_or_tokens_saved=False)
    wrong_key = base64.b64encode(secrets.token_bytes(32)).decode(); client.private_values.append(wrong_key.encode())
    bad_dns = re.sub(r'(?m)^\s*Endpoint\s*=.*$', 'Endpoint = xiao-unreachable-endpoint.invalid:51820', config)
    bad_psk = re.sub(r'(?m)^\s*PresharedKey\s*=.*$', 'PresharedKey = '+wrong_key, config)
    if bad_psk == config: bad_psk = bad_psk.replace('[Peer]', '[Peer]\nPresharedKey = '+wrong_key)

    def check(ok, name):
        report['checks'].append(dict(name=name, passed=bool(ok)))
        print(f"WG-FAILURE {'PASS' if ok else 'FAIL'} {name}", flush=True)
        if not ok: raise RuntimeError('WG failure check failed: '+name)

    def wait(predicate, path='/api/network', seconds=70):
        deadline = time.monotonic()+seconds
        while time.monotonic() < deadline:
            state = client.state(path)
            if predicate(state): return state
            time.sleep(.3)
        raise RuntimeError('WG failure state wait timed out: '+path)

    def upload(text):
        return client.request('/api/wireguard/import','POST',raw=text.encode(),headers={'Content-Type':'text/plain'})[0]

    def latest_files():
        date = time.strftime('%Y%m%d', time.gmtime())
        return {item['path']:item['bytes'] for item in client.state('/api/media?date='+date)['items']}

    def stop_collect(label, previous):
        check(client.request('/api/record/stop','POST')[0] == 202, label+'_stop_recording')
        wait(lambda state: not state['recording'] and not state['recordRequested'], '/api/status', seconds=8)
        current = latest_files(); added = [path for path in current if path not in previous and path.endswith('.avi')]
        check(len(added) == 1, label+'_owned_avi_finalized')
        path = added[0]; output = args.out/(label+'.avi')
        check(client.download('/api/media/download?path='+quote(path,safe=''), output) == current[path], label+'_complete_download')
        probe = subprocess.run(['C:/Program Files/ffmpeg/bin/ffprobe.exe','-v','error','-show_streams','-show_format','-of','json',str(output)], capture_output=True,text=True,check=True)
        result = validate(json.loads(probe.stdout))
        decoded = subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror','-i',str(output),'-f','null','-'], capture_output=True,text=True,timeout=60)
        check(result['passed'] and decoded.returncode == 0, label+'_mjpeg_pcm_complete_decode')
        report['media'].append(dict(label=label,path=path,bytes=current[path],duration_difference_s=result['duration_difference_s']))

    try:
        client.login(); check(True, 'chosen_account_login')
        if args.install_first:
            report['firmware'] = client.install(args.firmware)
            check(True, 'final_receiver_installed_over_authenticated_ota')
        check(client.state('/api/wireguard')['peerSessionUp'], 'existing_confirmed_peer_up')
        revision = client.state()['configRevision']
        for path in ('/api/wireguard/clear','/api/wireguard/restart'):
            check(client.request(path,'POST',authorized=False)[0] == 403, 'controls_require_session_'+path.rsplit('/',1)[-1])
            check(client.request(path+'?x=1','POST')[0] == 400, 'controls_reject_query_'+path.rsplit('/',1)[-1])
            check(client.request(path,'POST',raw=b'x')[0] == 400, 'controls_require_empty_body_'+path.rsplit('/',1)[-1])
        check(client.request('/api/wireguard/clear','POST')[0] == 202, 'clear_staged')
        network = wait(lambda state: state['state'] == 'awaiting_local_confirmation')
        check(client.state()['configRevision'] == revision and not client.state('/api/wireguard')['running'], 'unconfirmed_clear_keeps_active_and_lan')
        check(client.request('/api/network/confirm','POST',{'revision':network['revision']})[0] == 202, 'clear_confirmed_over_lan')
        wait(lambda state: state['state'] == 'ready' and not state['revision'])
        wg = client.state('/api/wireguard')
        check(not wg['enabled'] and not wg['privateKeySet'] and not wg['presharedKeySet'], 'confirmed_clear_removes_selected_key_fields')
        check(client.state()['cameraReady'] and client.state()['sdReady'], 'camera_sd_after_confirmed_clear')
        check(upload(config) == 202, 'original_private_config_reimported')
        network = wait(lambda state: state['state'] == 'awaiting_local_confirmation')
        check(client.request('/api/network/confirm','POST',{'revision':network['revision']})[0] == 202, 'original_peer_reconfirmed')
        wait(lambda state: state['state'] == 'ready' and not state['revision'])
        check(client.state('/api/wireguard')['peerSessionUp'], 'original_session_restored_after_clear')
        revision = client.state()['configRevision']
        for label, text, expected in (('bad_dns',bad_dns,('endpoint_dns_failed','endpoint_dns_timeout')),
                                      ('bad_psk',bad_psk,('peer_session_unavailable',))):
            previous = latest_files(); check(client.request('/api/record/start','POST')[0] == 202, label+'_recording_started')
            wait(lambda state: state['recording'], '/api/status', seconds=5)
            check(upload(text) == 202, label+'_trial_queued')
            wait(lambda state: state['revision'] != 0, seconds=5)
            wait(lambda state: not state['peerSessionUp'], '/api/wireguard', seconds=5)
            observed_error = False; deadline = time.monotonic()+70
            while time.monotonic() < deadline:
                started = time.monotonic(); status = client.state(); report['status_latencies_s'].append(time.monotonic()-started)
                if not status['recording'] or not status['cameraReady'] or status['configRevision'] != revision:
                    raise RuntimeError('Camera/active config changed during failed peer trial')
                wg = client.state('/api/wireguard'); observed_error |= wg['error'] in expected
                network = client.state('/api/network')
                if not network['revision'] and network['state'] == 'ready' and wg['peerSessionUp']: break
                time.sleep(.6)
            else: raise RuntimeError('Failed peer did not restore active network')
            check(observed_error, label+'_sanitized_error_visible')
            check(network['error'] == 'connect_timeout' and not network['rescueAP'], label+'_automatic_timeout_rollback_and_ap_closure')
            check(status['recording'] and status['configRevision'] == revision and wg['peerSessionUp'], label+'_lan_recording_and_original_peer_recover')
            check(client.request('/api/camera/still')[0] == 200, label+'_jpeg_after_recovery')
            stop_collect(label, previous)
        check(upload(bad_dns) == 202, 'interrupted_wg_trial_queued')
        wait(lambda state: state['revision'] != 0, seconds=5)
        check(client.request('/api/device/restart','POST')[0] == 202, 'restart_during_unconfirmed_wg_trial')
        client.wait_boot(); client.login(); check(True, 'chosen_account_after_interrupted_wg_reset')
        network = wait(lambda state: state['state'] == 'ready' and not state['revision'])
        check(network['error'] == 'interrupted' and client.state()['configRevision'] == revision, 'pending_wg_not_reapplied_on_boot')
        check(client.state('/api/wireguard')['peerSessionUp'] and client.state()['cameraReady'] and client.state()['sdReady'], 'old_wg_camera_sd_restore_after_interrupted_trial')
        check(max(report['status_latencies_s']) < 2, 'lan_status_responsive_during_bad_wg_trials')
        check(client.request('/api/auth/logout','POST')[0] == 200, 'final_logout')
        report['passed'] = True
    except Exception as error:
        report['error'] = type(error).__name__+': '+str(error); print(report['error'], flush=True)
        try:
            client.request('/api/record/stop','POST')
            if client.state('/api/network')['revision']: client.request('/api/network/cancel','POST')
        except OSError: pass
    finally:
        config = bad_dns = bad_psk = wrong_key = ''; client.clear(); report['check_count'] = len(report['checks'])
        (args.out/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__': raise SystemExit(main())
