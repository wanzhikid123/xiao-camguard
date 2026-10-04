"""Bounded RAM-only WG-B routing/egress spike, followed by normal-image restore.

Uses the user's private config in RAM to determine the expected public server
exit. Never promotes a full-tunnel setting or changes the selected NVS config.
Cold bootstrap, trial UX and lifetime/DHCP recovery are separate required gates.
"""
import argparse
import ipaddress
import json
from pathlib import Path
import re
import socket
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
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--restore', type=Path, required=True)
    parser.add_argument('--resolver', default='1.1.1.1')
    parser.add_argument('--out', type=Path, default=ROOT/'.tools/hardware/wg-full-spike')
    args = parser.parse_args(); args.out.mkdir(parents=True, exist_ok=True)
    client = Client(args.address, ROOT/'.tools/private/admin-password.txt')
    report = dict(passed=False, checks=[], credentials_or_tokens_saved=False,
                  product_full_tunnel_verified=False)
    installed = owned_record = False
    config = (ROOT/'XIAO_ESP32_Camera.conf').read_text(encoding='utf-8-sig')
    endpoint = re.search(r'^\s*Endpoint\s*=\s*([^\r\n]+)', config, re.M)
    if not endpoint: raise RuntimeError('Private config has no endpoint')
    hostname = endpoint.group(1).strip().rsplit(':', 1)[0]
    expected = {str(ipaddress.IPv4Address(item[4][0]))
                for item in socket.getaddrinfo(hostname, None, socket.AF_INET, socket.SOCK_DGRAM)}
    for name in ('PrivateKey', 'PublicKey', 'PresharedKey'):
        value = re.search(r'^\s*'+name+r'\s*=\s*(\S+)', config, re.M)
        if value: client.private_values.append(value.group(1).encode())
    config = ''

    def check(ok, name):
        report['checks'].append(dict(name=name, passed=bool(ok)))
        print('WG-B SPIKE', 'PASS' if ok else 'FAIL', name, flush=True)
        if not ok: raise RuntimeError('WG-B spike assertion: '+name)

    def wait(predicate, path='/api/wireguard/full-test', seconds=25):
        deadline = time.monotonic()+seconds
        while time.monotonic() < deadline:
            status = client.state(path)
            if predicate(status): return status
            time.sleep(.25)
        raise RuntimeError('WG-B spike wait deadline')

    def jpeg(label):
        code, _, data = client.request('/api/camera/still', timeout=30)
        check(code == 200 and data.startswith(b'\xff\xd8'), label+'_fresh_jpeg')
        path = args.out/(label+'.jpg'); path.write_bytes(data)
        result = subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe', '-v', 'error', '-xerror',
                                 '-i', str(path), '-f', 'null', '-'], capture_output=True, timeout=30)
        check(result.returncode == 0, label+'_whole_jpeg_decode')

    def inventory():
        dates = []; items = {}; cursor = ''
        for _ in range(32):
            data = client.state('/api/media/dates'+('?before='+quote(cursor,safe='') if cursor else ''))
            if data.get('enumerationTruncated'): raise RuntimeError('Incomplete date enumeration')
            dates.extend(item['date'] for item in data['items'])
            if not data['more']: break
            cursor = data['nextBefore']
        else: raise RuntimeError('Date enumeration bound')
        for date in dates:
            cursor = ''
            for _ in range(128):
                data = client.state('/api/media?date='+date+('&before='+quote(cursor,safe='') if cursor else ''))
                if data.get('enumerationTruncated'): raise RuntimeError('Incomplete media enumeration')
                items.update({item['path']:item['bytes'] for item in data['items']})
                if not data['more']: break
                cursor = data['nextBefore']
            else: raise RuntimeError('Media enumeration bound')
        return items

    try:
        client.login(); check(True, 'chosen_account_login')
        before = client.state(); values = client.state('/api/settings/capture')['values']
        old_media = inventory()
        check(before['cameraReady'] and before['sdReady'] and not before['recording'], 'normal_camera_sd_idle')
        installed = True; report['fixture_installation'] = client.install(args.fixture)
        check(True, 'authenticated_fixture_ota_and_login')
        check(client.request('/api/wireguard/full-test', authorized=False)[0] == 401, 'fixture_status_requires_session')
        check(client.request('/api/wireguard/full-test', 'POST', {'action':'start','resolver':args.resolver},
                             headers={'X-CSRF-Token':''})[0] == 403, 'fixture_requires_csrf')
        check(client.request('/api/wireguard/full-test?extra=1', 'POST', {'action':'stop'})[0] == 400, 'fixture_rejects_query')
        for body in ({'action':'start'}, {'action':'stop','extra':1}, {'action':'unknown'}):
            check(client.request('/api/wireguard/full-test', 'POST', body)[0] == 400, 'fixture_rejects_invalid_schema')
        wait(lambda status:status['peerSessionUp'], '/api/wireguard')
        original_dns = client.state('/api/wireguard/full-test').get('dnsServers')
        initial_wg = client.state('/api/wireguard')
        check(client.request('/api/wireguard/full-test', 'POST', {'action':'start','resolver':args.resolver})[0] == 202,
              'owner_queued_ephemeral_full_mode')
        full = wait(lambda status:status['active'] and status['outboundDone'], seconds=35)
        report['full_observation'] = full
        check(full['hardwareFixture'] and not full['productFullTunnelVerified'], 'fixture_does_not_claim_product_readiness')
        check(full['guardedInterfaces'] >= 1 and full['tunnelPresent'], 'physical_outputs_guarded')
        check(full['packetChecks'] >= 18 and full['packetFailures'] == 0, 'actual_output_guard_packet_cases')
        check(full['ipv4Blocked'] >= 9 and full['ipv6Blocked'] >= 3, 'wan_dns_fragment_and_ipv6_denials')
        check(full['outboundHTTP'] == 200 and full['outboundAddress'] in expected, 'ordinary_unbound_http_has_expected_server_exit')
        after_wg = client.state('/api/wireguard')
        tx = after_wg['sentBytes']-initial_wg['sentBytes']; rx = after_wg['receivedBytes']-initial_wg['receivedBytes']
        report['public_exit_wg_bytes'] = dict(sent=tx, received=rx)
        check(tx > 0 and rx > 0 and full['routedTunnel'] > 0, 'public_request_has_real_wg_data_deltas')
        check(full.get('dnsServers') == [args.resolver,'0.0.0.0','0.0.0.0'], 'explicit_resolver_no_dhcp_fallback')
        check(client.state()['configRevision'] == before['configRevision'], 'full_spike_never_persists_mode')
        jpeg('full-active')
        check(client.request('/api/record/start','POST')[0] == 202, 'manual_audio_recording_under_full_routes')
        owned_record = True
        wait(lambda status:status['recording'] and status['audioInputRunning'], '/api/status', 8)
        time.sleep(8)
        check(client.request('/api/record/stop','POST')[0] == 202, 'recording_stops_under_full_routes')
        wait(lambda status:not status['recording'] and not status['recordRequested'], '/api/status', 12)
        owned_record = False
        media = inventory(); clips = [path for path in media if path not in old_media and path.endswith('.avi')]
        check(len(clips) == 1, 'one_owned_full_routing_clip')
        target = args.out/'full-routing.avi'
        check(client.download('/api/media/download?path='+quote(clips[0],safe=''),target) == media[clips[0]],
              'whole_full_routing_clip_readback')
        probe = subprocess.run(['C:/Program Files/ffmpeg/bin/ffprobe.exe','-v','error','-show_streams',
                                '-show_format','-of','json',str(target)],capture_output=True,timeout=30,check=True)
        avi = validate(json.loads(probe.stdout))
        decoded = subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror',
                                  '-i',str(target),'-f','null','-'],capture_output=True,timeout=45)
        check(avi['passed'] and decoded.returncode == 0, 'complete_full_routing_mjpeg_pcm_decode')
        report['recording'] = avi
        check(client.request('/api/wireguard/full-test','POST',{'action':'hold'})[0] == 202, 'owner_stops_tunnel_keeps_guard')
        down = wait(lambda status:status['active'] and not status['tunnelPresent'] and status['outboundDone'], seconds=35)
        report['down_observation'] = down
        check(down['outboundHTTP'] != 200 and down['outboundAddress'] == '0.0.0.0', 'ordinary_wan_unavailable_without_tunnel')
        check(down['ipv4Blocked'] > full['ipv4Blocked'], 'absent_tunnel_physical_fallback_is_actually_blocked')
        jpeg('full-down')
        wait(lambda status:status['peerSessionUp'], '/api/wireguard', 65)
        check(client.state('/api/wireguard/full-test')['tunnelPresent'], 'recreated_interface_attaches_to_full_policy')
        check(client.request('/api/wireguard/full-test','POST',{'action':'stop'})[0] == 202, 'explicit_fixture_stop')
        stopped = wait(lambda status:not status['active'])
        check(stopped.get('dnsServers') == original_dns, 'all_original_resolvers_restored')
        check(client.state('/api/settings/capture')['values'] == values, 'all_original_capture_values_preserved')
        check(client.state()['configRevision'] == before['configRevision'], 'network_revision_preserved')
        report['passed'] = True
    except Exception as error:
        report['passed'] = False; report['error_type'] = type(error).__name__
        print('WG-B SPIKE FAIL', type(error).__name__, flush=True)
    finally:
        try:
            if owned_record:
                client.request('/api/record/stop','POST')
                wait(lambda status:not status['recording'] and not status['recordRequested'], '/api/status', 15)
            if installed:
                if not client.cookie: client.login()
                client.request('/api/wireguard/full-test','POST',{'action':'stop'})
                wait(lambda status:not status['active'])
                report['normal_restoration'] = client.install(args.restore)
                check(client.request('/api/wireguard/full-test')[0] == 404, 'normal_image_has_no_spike_api')
                check(client.state()['configRevision'] == before['configRevision']
                      and client.state('/api/settings/capture')['values'] == values, 'normal_restore_preserves_values_revision')
                check(client.state()['cameraReady'] and client.state()['sdReady']
                      and client.state('/api/wireguard')['peerSessionUp'], 'normal_camera_sd_wg_healthy')
                media = inventory()
                check(all(media.get(path) == size for path,size in old_media.items()), 'all_original_media_preserved')
                report['original_media_count'] = len(old_media)
            if client.cookie: client.request('/api/auth/logout','POST')
        except Exception as error:
            report['passed'] = False; report['cleanup_error_type'] = type(error).__name__
            print('WG-B SPIKE cleanup FAIL', type(error).__name__, flush=True)
        client.clear(); report['check_count'] = len(report['checks'])
        (args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return 0 if report['passed'] else 1


if __name__ == '__main__': raise SystemExit(main())
