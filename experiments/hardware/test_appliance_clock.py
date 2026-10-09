"""Check actual saved photo clocks against Berlin time; remove only owned media."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import subprocess
import time
from urllib.parse import quote

from appliance_client import Client
from full_mode_tools import inventory, decode_avi

ROOT = Path(__file__).resolve().parents[2]


def berlin_now():
    # Windows has the Berlin DST database even without Python's optional tzdata.
    command = "[TimeZoneInfo]::ConvertTimeFromUtc([DateTime]::UtcNow,[TimeZoneInfo]::FindSystemTimeZoneById('W. Europe Standard Time')).ToString('yyyy-MM-dd HH:mm:ss')"
    value = subprocess.check_output(['powershell', '-NoProfile', '-Command', command], text=True).strip()
    return datetime.strptime(value, '%Y-%m-%d %H:%M:%S')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', default='192.168.1.181')
    parser.add_argument('--firmware', type=Path)
    parser.add_argument('--reboot', action='store_true')
    parser.add_argument('--video', action='store_true')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'checks': [], 'owned': []}
    client = Client(args.address, ROOT / '.tools/private/admin-password.txt')
    baseline = None
    original_conversion = None
    recording_owned = False

    def check(ok, name):
        report['checks'].append({'name': name, 'passed': bool(ok)})
        print('CLOCK', 'PASS' if ok else 'FAIL', name, flush=True)
        if not ok:
            raise AssertionError(name)

    try:
        client.login(progress=lambda phase, seconds: print('LOGIN', phase, seconds, flush=True))
        state = client.state()
        check(not state['recording'] and not state['recordRequested'] and not state['events']['requested']
              and not state['timelapse']['active'], 'device_idle_for_clock_test')
        baseline = inventory(client)
        baseline_capture = client.state('/api/settings/capture')['values']
        baseline_revision = state['configRevision']
        if args.firmware:
            report['installation'] = client.install(args.firmware)
        if args.reboot:
            check(client.request('/api/device/restart', 'POST')[0] == 202, 'restart_accepted')
            client.wait_boot()
            client.login()
        deadline = time.monotonic() + 30
        while not (state := client.state())['timeSynchronized'] and time.monotonic() < deadline:
            time.sleep(.5)
        check(state['cameraReady'] and state['sdReady'] and not state['startupFailed'], 'healthy_camera_and_storage')
        check(client.state('/api/settings/capture')['values'] == baseline_capture, 'capture_settings_preserved')
        check(state['configRevision'] == baseline_revision, 'network_configuration_revision_preserved')
        for route, method in (('/api/events/test', 'POST'), ('/api/codecs/test', 'GET')):
            check(client.request(route, method)[0] == 404, 'normal_firmware_fixture_absent_' + route)
        check(state['wifiConnected'] and state['timeSynchronized'], 'wifi_clock_synchronized_without_browser_clock')
        report['clockStatus'] = {key: state[key] for key in ('timeSynchronized', 'timezone', 'localTime', 'epochSeconds') if key in state}
        if 'epochSeconds' in state:
            started = time.time()
            state = client.state()
            check(started - 3 <= state['epochSeconds'] <= time.time() + 3, 'utc_clock_matches_host')
            local_reference = berlin_now()
            local_clock = datetime.strptime(state['localTime'], '%Y-%m-%d %H:%M:%S')
            check(abs((local_clock - local_reference).total_seconds()) <= 5, 'status_local_time_matches_berlin')
            check(state['timezone'] == 'CET-1CEST,M3.5.0/2,M10.5.0/3', 'berlin_rule_survives_legacy_reload')
        start = berlin_now()
        deadline = time.monotonic() + 30
        while True:
            code, _, body = client.request('/api/camera/photo', 'POST', timeout=30)
            result = json.loads(body)
            if code != 409 or time.monotonic() >= deadline:
                break
            time.sleep(.5)
        report['photoResponse'] = {'code': code, 'error': result.get('error')}
        print('CLOCK photoResponse', report['photoResponse'], flush=True)
        check(code == 201, 'real_photo_saved')
        path = json.loads(body)['path']
        check(path not in baseline, 'photo_is_owned_by_this_run')
        report['owned'].append(path)
        end = berlin_now()
        match = re.fullmatch(r'/\d{8}/(\d{8}_\d{6})_[0-9a-f]{6}_photo\.jpg', path)
        check(bool(match), 'photo_filename_has_capture_clock')
        captured = datetime.strptime(match.group(1), '%Y%m%d_%H%M%S')
        report['photoClock'] = captured.isoformat()
        report['expectedBerlinClock'] = start.isoformat()
        report['offsetSeconds'] = (captured - start).total_seconds()
        print('CLOCK offsetSeconds', report['offsetSeconds'], flush=True)
        check(-3 <= report['offsetSeconds'] <= (end - start).total_seconds() + 3, 'photo_matches_berlin_local_time')
        if args.video:
            original_conversion = client.state('/api/settings/transcode')['enabled']
            check(client.request('/api/settings/transcode', 'POST', {'enabled': False})[0] == 200, 'conversion_paused_for_owned_video')
            before_video = inventory(client)
            start = berlin_now()
            check(client.request('/api/record/start', 'POST')[0] == 202, 'owned_video_start_accepted')
            recording_owned = True
            deadline = time.monotonic() + 20
            while not client.state()['recording'] and time.monotonic() < deadline:
                time.sleep(.2)
            check(client.state()['recording'], 'owned_video_recording')
            time.sleep(6)
            check(client.request('/api/record/stop', 'POST')[0] == 202, 'owned_video_stop_accepted')
            deadline = time.monotonic() + 30
            while client.state()['recording'] and time.monotonic() < deadline:
                time.sleep(.2)
            check(not client.state()['recording'], 'owned_video_finalized')
            recording_owned = False
            current = inventory(client)
            added = set(current) - set(before_video)
            check(len(added) == 1 and next(iter(added)).endswith('.avi'), 'exactly_one_owned_video')
            path = added.pop()
            report['owned'].append(path)
            match = re.match(r'/\d{8}/(\d{8}_\d{6})_', path)
            check(bool(match), 'video_filename_has_capture_clock')
            captured = datetime.strptime(match.group(1), '%Y%m%d_%H%M%S')
            report['videoClock'] = captured.isoformat()
            report['videoOffsetSeconds'] = (captured - start).total_seconds()
            check(-3 <= report['videoOffsetSeconds'] <= 20, 'video_matches_berlin_local_time')
            target = args.out.parent / (args.out.stem + '-owned.avi')
            client.download('/api/media/download?path=' + quote(path, safe=''), target)
            report['videoDecode'] = decode_avi(target)
            check(report['videoDecode']['passed'] and report['videoDecode']['whole_decode_passed'], 'owned_video_full_audio_video_decode')
        report['passed'] = True
    except Exception as failure:
        report['error'] = str(failure)
    finally:
        try:
            if recording_owned:
                client.request('/api/record/stop', 'POST')
                deadline = time.monotonic() + 30
                while client.state()['recording'] and time.monotonic() < deadline:
                    time.sleep(.2)
            for path in report['owned']:
                check(path not in baseline, 'cleanup_ownership_guard')
                check(client.request('/api/media/delete', 'POST', {'path': path})[0] == 200, 'owned_media_removed')
            if baseline is not None:
                current = inventory(client)
                check(all(current.get(path) == size for path, size in baseline.items()), 'original_media_preserved')
        except Exception as failure:
            report['passed'] = False
            report['cleanupError'] = str(failure)
        if original_conversion is not None:
            try:
                check(client.request('/api/settings/transcode', 'POST', {'enabled': original_conversion})[0] == 200, 'original_conversion_setting_restored')
            except Exception as failure:
                report['passed'] = False
                report['restoreError'] = str(failure)
        client.clear()
        args.out.write_text(json.dumps(report, indent=2), encoding='utf8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
