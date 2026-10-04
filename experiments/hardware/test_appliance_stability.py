"""Five-minute WG-active audio AVI, then 30-minute idle camera/network gate.

No network configuration, existing media or device reboot is changed. Only
this run's new recording is downloaded. Private values/tokens stay in RAM.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
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
    parser.add_argument('--record-seconds', type=int, default=300)
    parser.add_argument('--idle-minutes', type=int, default=30)
    parser.add_argument('--out', type=Path, default=ROOT/'.tools/hardware/appliance-stability')
    parser.add_argument('--existing-recording', help='Reuse a specifically named, already-finalized owned recording; read-only')
    parser.add_argument('--existing-download', type=Path, help='Reuse its previously verified local download; avoids downloading a large clip again for the idle gate')
    args = parser.parse_args(); args.out.mkdir(parents=True, exist_ok=True)
    client = Client(args.address, args.password_file)
    report = dict(passed=False, stage='login', checks=[], samples=[], credentials_or_tokens_saved=False,
                  record_seconds=args.record_seconds, idle_minutes=args.idle_minutes)

    def save(): (args.out/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

    def check(ok, name):
        report['checks'].append(dict(name=name, passed=bool(ok)))
        print(f"STABILITY {'PASS' if ok else 'FAIL'} {name}", flush=True)
        if not ok: raise RuntimeError('Stability check failed: '+name)

    def files():
        date = time.strftime('%Y%m%d', time.gmtime()); cursor=''; items={}
        for _ in range(32):
            page=client.state('/api/media?date='+date+('&before='+quote(cursor,safe='') if cursor else ''))
            items.update({item['path']:item['bytes'] for item in page['items']})
            if not page['more']:return items
            cursor=page['nextBefore']
        raise RuntimeError('Media enumeration bound')

    try:
        client.login(); check(True, 'chosen_account_login')
        before = client.state(); revision = before['configRevision']
        check(before['cameraReady'] and before['sdReady'] and client.state('/api/wireguard')['peerSessionUp'], 'camera_sd_wg_initially_available')
        if args.existing_recording:
            check(not before['recording'] and not before['recordRequested'], 'owned_recording_already_finalized')
            current = files(); path = args.existing_recording
            check(path in current and path.endswith('.avi'), 'explicit_owned_recording_available')
            report['recording_reused_read_only'] = True
        else:
            previous = files(); report['stage'] = 'recording'; save()
            check(client.request('/api/record/start','POST')[0] == 202, 'five_minute_recording_accepted')
            started = time.monotonic(); announced = 0
            while time.monotonic()-started < args.record_seconds:
                state = client.state()
                if time.monotonic()-started > 3 and (not state['recording'] or not state['cameraReady']):
                    raise RuntimeError('Long recording stopped unexpectedly')
                elapsed = int(time.monotonic()-started)
                if elapsed//60 > announced:
                    announced = elapsed//60; print('STABILITY recording elapsed minutes', announced, flush=True)
                time.sleep(2)
            check(client.request('/api/record/stop','POST')[0] == 202, 'long_recording_stop_accepted')
            # PCM is appended at close. The observed 300 s / 9.2 MB audio close
            # took 18.3 s on this card, so a fixed 10 s gate was incorrect.
            finalizing = time.monotonic(); deadline = finalizing+max(30, args.record_seconds*.2)
            while time.monotonic() < deadline:
                state = client.state()
                if not state['recording'] and not state['recordRequested']: break
                time.sleep(.3)
            report['finalization_s'] = time.monotonic()-finalizing
            check(not state['recording'] and not state['recordRequested'], 'long_recording_finalized')
            current = files(); added = [path for path in current if path not in previous and path.endswith('.avi')]
            check(len(added) == 1, 'one_owned_long_recording')
            path = added[0]
        output = args.existing_download or args.out/'five-minute-recording.avi'
        if args.existing_download:
            check(bool(args.existing_recording) and output.is_file() and output.stat().st_size == current[path], 'previous_local_download_size_matches_owned_recording')
            report['download_reused_read_only'] = True
            report['local_download_sha256'] = hashlib.sha256(output.read_bytes()).hexdigest()
        else:
            check(client.download('/api/media/download?path='+quote(path,safe=''), output) == current[path], 'complete_long_avi_download')
        probe = subprocess.run(['C:/Program Files/ffmpeg/bin/ffprobe.exe','-v','error','-show_streams','-show_format','-of','json',str(output)], capture_output=True,text=True,check=True,timeout=60)
        media = validate(json.loads(probe.stdout))
        decoded = subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror','-i',str(output),'-f','null','-'], capture_output=True,text=True,timeout=120)
        check(media['passed'] and decoded.returncode == 0 and media['durations_s']['video'] >= args.record_seconds-3, 'long_mjpeg_pcm_contract_duration_and_complete_decode')
        report['long_recording'] = dict(path=path,bytes=current[path],durations_s=media['durations_s'],duration_difference_s=media['duration_difference_s'])
        check(client.state('/api/wireguard')['peerSessionUp'], 'wg_peer_after_long_audio_recording')
        report['stage'] = 'idle'; save(); started = time.monotonic(); idle_before = client.state(); announced = 0
        capture=client.state('/api/settings/capture')['values']
        while time.monotonic()-started < args.idle_minutes*60:
            request_started = time.monotonic(); state = client.state(); latency = time.monotonic()-request_started
            wg = client.state('/api/wireguard'); elapsed = time.monotonic()-started
            if not state['cameraReady'] or not state['sdReady'] or not state['wifiConnected'] or state['recording']:
                raise RuntimeError('Idle camera/SD/underlay state failed')
            if state['configRevision'] != revision or state['uptimeSeconds'] < idle_before['uptimeSeconds']:
                raise RuntimeError('Device reset or selected config changed during idle')
            report['samples'].append(dict(elapsed_s=round(elapsed,3),uptime_s=state['uptimeSeconds'],
                free_heap=state['freeHeap'],free_psram=state['freePsram'],temperature_c=state['temperatureC'],
                status_latency_s=round(latency,4),capture_idle=state['captureIdle'],wg_phase=wg['phase'],wg_peer_session_up=wg['peerSessionUp']))
            save()
            if int(elapsed)//60 > announced:
                announced = int(elapsed)//60; print('STABILITY idle elapsed minutes',announced,'heap',state['freeHeap'],flush=True)
            time.sleep(min(10, max(0, args.idle_minutes*60-(time.monotonic()-started))))
        elapsed = time.monotonic()-started; final = client.state()
        check(elapsed >= args.idle_minutes*60, 'full_requested_idle_interval_completed')
        check(final['uptimeSeconds'] >= idle_before['uptimeSeconds']+elapsed-10, 'no_idle_reset_or_uptime_loss')
        # Compare after standby has stabilized so the camera's cache release
        # cannot conceal a subsequent memory leak.
        warm_at=60
        if capture['powerSave']:
            first_idle=next((s['elapsed_s'] for s in report['samples'] if s['capture_idle']),None)
            check(first_idle is not None,'power_saving_enters_standby_during_idle')
            warm_at=first_idle+30
        warm = [sample['free_heap'] for sample in report['samples'] if warm_at <= sample['elapsed_s'] < warm_at+60]
        tail = [sample['free_heap'] for sample in report['samples'] if sample['elapsed_s'] >= elapsed-70]
        check(min(sample['free_heap'] for sample in report['samples']) > 65536
              and statistics.median(tail) >= statistics.median(warm)-8192, 'no_observed_idle_heap_collapse')
        check(min(sample['free_psram'] for sample in report['samples']) > 2000000, 'psram_retains_recording_margin')
        check(max(sample['status_latency_s'] for sample in report['samples']) < 2, 'status_remains_responsive_during_idle')
        # No WG data probe ran during idle. After it, confirm actual fresh data,
        # allowing a short natural key-rotation gap before admission.
        deadline = time.monotonic()+15
        while not client.state('/api/wireguard')['peerSessionUp'] and time.monotonic() < deadline: time.sleep(.5)
        before_probe = client.state('/api/wireguard')
        check(client.request('/api/wireguard/probe','POST',{'address':'10.8.0.1'})[0] == 202, 'post_idle_destination_probe_accepted')
        deadline = time.monotonic()+8
        while time.monotonic() < deadline:
            wg = client.state('/api/wireguard')
            if not wg['probeBusy'] and wg['probeSent'] == 3 and wg['receivedBytes'] > before_probe['receivedBytes']: break
            time.sleep(.3)
        check(wg['probeReceived'] > 0 and wg['sentBytes'] > before_probe['sentBytes']
              and wg['receivedBytes'] > before_probe['receivedBytes'], 'authenticated_wg_route_has_replies_after_idle')
        check(client.request('/api/camera/still')[0] == 200, 'camera_jpeg_after_idle')
        check(client.request('/api/auth/logout','POST')[0] == 200, 'final_logout')
        report['idle_elapsed_s'] = elapsed; report['passed'] = True; report['stage'] = 'complete'
    except Exception as error:
        report['error'] = type(error).__name__+': '+str(error); print(report['error'],flush=True)
        try: client.request('/api/record/stop','POST')
        except OSError: pass
    finally:
        client.clear(); report['check_count'] = len(report['checks']); save()
    return 0 if report['passed'] else 1


if __name__ == '__main__': raise SystemExit(main())
