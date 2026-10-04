"""Twenty real camera/audio/WG-A lifecycle cycles, with three Wi-Fi losses.

Only unconfirmed network trials are changed. Existing media are preserved;
every new AVI is downloaded, inspected and completely decoded privately.
"""
import argparse
import http.client
import json
from pathlib import Path
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
    parser.add_argument('--out', type=Path, default=ROOT/'.tools/hardware/appliance-wg-cycles')
    parser.add_argument('--probe-address', default='10.8.0.1')
    parser.add_argument('--cycles', type=int, default=20)
    args = parser.parse_args(); args.out.mkdir(parents=True, exist_ok=True)
    client = Client(args.address, args.password_file)
    report = dict(passed=False, checks=[], cycles=[], credentials_or_tokens_saved=False)
    ffprobe = 'C:/Program Files/ffmpeg/bin/ffprobe.exe'
    ffmpeg = 'C:/Program Files/ffmpeg/bin/ffmpeg.exe'

    def check(ok, name):
        report['checks'].append(dict(name=name, passed=bool(ok)))
        print(f"WG-CYCLES {'PASS' if ok else 'FAIL'} {name}", flush=True)
        if not ok: raise RuntimeError('Lifecycle check failed: '+name)

    def wait(predicate, path='/api/network', seconds=20, tolerate_loss=False):
        deadline = time.monotonic()+seconds
        while time.monotonic() < deadline:
            try:
                state = client.state(path)
                if predicate(state): return state
            except (OSError, http.client.HTTPException):
                if not tolerate_loss: raise
            time.sleep(.4)
        raise RuntimeError('Lifecycle state wait timed out: '+path)

    def inventory():
        dates, items, before = [], {}, ''
        for _ in range(10):
            page = client.state('/api/media/dates'+('?before='+before if before else ''))
            if page['enumerationTruncated']: raise RuntimeError('Media date enumeration truncated')
            dates.extend(item['date'] for item in page['items']); before = page['nextBefore']
            if not before: break
        for date in dates:
            before = ''
            for _ in range(20):
                page = client.state('/api/media?date='+date+('&before='+before if before else ''))
                if page['enumerationTruncated']: raise RuntimeError('Media file enumeration truncated')
                items.update({item['path']: item['bytes'] for item in page['items']})
                before = page['nextBefore']
                if not before: break
        return items

    try:
        client.login(); check(True, 'chosen_account_login')
        initial = client.state(); revision = initial['configRevision']
        check(client.state('/api/wireguard')['peerSessionUp'], 'confirmed_wg_peer_present')
        originals = inventory(); report['original_count'] = len(originals)
        previous = originals.copy()
        for cycle in range(1, args.cycles+1):
            prefix = f'cycle_{cycle:02d}_'; started = time.monotonic()
            check(client.request('/api/record/start', 'POST')[0] == 202, prefix+'record_start')
            wait(lambda state: state['recording'], '/api/status', seconds=5)
            check(client.request('/api/wireguard/disable', 'POST')[0] == 202, prefix+'disable_trial_accepted')
            wait(lambda state: state['state'] == 'awaiting_local_confirmation')
            wg = client.state('/api/wireguard'); state = client.state()
            check(not wg['running'] and not wg['peerSessionUp'] and state['recording'] and state['cameraReady'], prefix+'lan_recording_survive_wg_stop')
            check(client.request('/api/camera/still')[0] == 200, prefix+'jpeg_during_wg_disabled_trial')
            check(client.request('/api/network/cancel', 'POST')[0] == 202, prefix+'cancel_unconfirmed_disable')
            wait(lambda state: state['state'] == 'ready' and not state['revision'])
            wg = client.state('/api/wireguard')
            check(wg['peerSessionUp'] and wg['running'], prefix+'new_authenticated_peer_session')
            check(client.request('/api/wireguard/probe', 'POST', {'address': args.probe_address})[0] == 202, prefix+'destination_probe_queued')
            probe = wait(lambda state: not state['probeBusy'] and state['probeSent'] == 3,
                         '/api/wireguard', seconds=8)
            check(probe['probeReceived'] > 0 and probe['sentBytes'] > wg['sentBytes']
                  and probe['receivedBytes'] > wg['receivedBytes'], prefix+'authenticated_route_data')
            reconnected = cycle in (5, 10, 15)
            if reconnected:
                # A nonexistent trial forces loss, then the normal 60 s timeout
                # restores the unchanged active Wi-Fi/WG config. No PC VPN edits.
                check(client.request('/api/network/trial', 'POST',
                      {'ssid':'XIAO-No-Such-Network-084C', 'wifiPassword':'bounded-test-network'})[0] == 202,
                      prefix+'failed_wifi_trial_queued_during_recording')
                time.sleep(2)
                wait(lambda state: state['state'] == 'ready' and not state['revision']
                     and state['error'] == 'connect_timeout', seconds=85, tolerate_loss=True)
                state = client.state()
                check(state['recording'] and state['cameraReady'] and client.state('/api/wireguard')['peerSessionUp'],
                      prefix+'wifi_wg_recover_without_interrupting_recording')
            time.sleep(max(0, 12-(time.monotonic()-started)))
            check(client.request('/api/record/stop', 'POST')[0] == 202, prefix+'record_stop')
            wait(lambda state: not state['recording'] and not state['recordRequested'], '/api/status', seconds=8)
            current = inventory()
            added = [path for path in current if path not in previous and path.endswith('.avi')]
            check(len(added) == 1, prefix+'one_finalized_owned_avi')
            path = added[0]; output = args.out/f'cycle-{cycle:02d}.avi'
            check(client.download('/api/media/download?path='+quote(path, safe=''), output) == current[path], prefix+'complete_avi_download')
            inspected = subprocess.run([ffprobe,'-v','error','-show_streams','-show_format','-of','json',str(output)],
                                       capture_output=True, text=True, check=True, timeout=30)
            decoded = subprocess.run([ffmpeg,'-v','error','-xerror','-i',str(output),'-f','null','-'],
                                     capture_output=True, text=True, timeout=60)
            media = validate(json.loads(inspected.stdout))
            check(media['passed'] and decoded.returncode == 0, prefix+'mjpeg_pcm_contract_and_full_decode')
            check(all(current.get(path) == size for path, size in originals.items()), prefix+'original_media_preserved')
            state = client.state()
            check(state['configRevision'] == revision and client.state('/api/wireguard')['peerSessionUp'], prefix+'active_config_unchanged_and_peer_up')
            report['cycles'].append(dict(cycle=cycle, wifi_reconnect=reconnected, owned_avi=path,
                bytes=current[path], free_heap=state['freeHeap'], free_psram=state['freePsram'],
                video_duration_s=media['durations_s']['video'], audio_duration_s=media['durations_s']['audio'],
                duration_difference_s=media['duration_difference_s'], elapsed_s=round(time.monotonic()-started, 3)))
            previous = current
            (args.out/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        check(client.request('/api/auth/logout', 'POST')[0] == 200, 'final_logout')
        report['passed'] = True
    except Exception as error:
        report['error'] = type(error).__name__+': '+str(error); print(report['error'], flush=True)
        # Stop only the recording owned by this run, retaining all media.
        try: client.request('/api/record/stop', 'POST')
        except (OSError, http.client.HTTPException): pass
    finally:
        report['check_count'] = len(report['checks']); report['maximum_http_transaction_s'] = round(client.maximum_latency, 3)
        client.clear(); (args.out/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__': raise SystemExit(main())
