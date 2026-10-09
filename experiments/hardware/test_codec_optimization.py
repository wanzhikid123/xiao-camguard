"""Owned short-clip regression for codec memory and 64-bit MP4 clocks.

The clock64 fixture changes only a newly recorded test AVI. It reaches the
real long-clock finalization path without a long recording or encoder run.
Always restore the specified normal firmware and the original configuration.
"""
import argparse
from fractions import Fraction
import json
from pathlib import Path
import subprocess
import time
from urllib.parse import quote
from appliance_client import Client
from full_mode_tools import inventory

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--firmware', type=Path, required=True)
    p.add_argument('--restore', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--red', action='store_true')
    p.add_argument('--pressure', action='store_true')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    c = Client('192.168.1.181', ROOT / '.tools/private/admin-password.txt')
    report = {'passed': False, 'checks': [], 'clips': []}
    before = capture = enabled = None
    owned = set()

    def check(ok, name):
        report['checks'].append({'name': name, 'passed': bool(ok)})
        print('CODEC', 'PASS' if ok else 'FAIL', name, flush=True)
        if not ok:
            report['passed'] = False
            raise AssertionError(name)

    def wait_state(predicate, timeout=30):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            state = c.state()
            if predicate(state):
                return state
            time.sleep(.3)
        raise AssertionError('State deadline')

    def probe(path):
        return json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_streams', '-show_format', '-of', 'json', str(path)]))

    try:
        c.login()
        state = c.state()
        check(not state['recording'] and not state['events']['armed'] and not state.get('timelapse', {}).get('active'), 'device_idle')
        enabled = state['conversion']['enabled']
        capture = c.state('/api/settings/capture')['values']
        check(c.request('/api/settings/transcode', 'POST', {'enabled': False})[0] == 200, 'protect_original_media')
        before = inventory(c)
        report['originalCount'] = len(before)
        report['fixture'] = c.install(args.firmware)
        check(c.request('/api/codecs/test')[0] == 200, 'fixture_available')
        if not args.red:
            core = c.state('/api/codecs/checks')
            check(core['passed'] == core['total'] == 13, 'clock_pixel_memory_core_checks')
            report['coreChecks'] = core
        for number, (width, clock64) in enumerate([(640, True)] if args.red else [(640, False), (320, False), (640, True)]):
            settings = c.state('/api/settings/capture')
            resolution = next(x['value'] for x in settings['videoResolutions'] if x['width'] == width)
            check(c.request('/api/settings/capture', 'POST', {'values': {'videoResolution': resolution, 'fps': 5, 'cpuMHz': 240, 'powerSave': 0}})[0] == 200, 'settings_' + str(number))
            local_before = inventory(c)
            check(c.request('/api/record/start', 'POST')[0] == 202, 'start_' + str(number))
            wait_state(lambda s: s['recording'])
            time.sleep(6)
            check(c.request('/api/record/stop', 'POST')[0] == 202, 'stop_' + str(number))
            wait_state(lambda s: not s['recording'] and not s['recordRequested'])
            paths = set(inventory(c)) - set(local_before)
            owned.update(paths)
            check(len(paths) == 1, 'one_owned_avi_' + str(number))
            source = next(iter(paths))
            if clock64:
                check(c.request('/api/codecs/test', 'POST', {'path': source, 'mode': 'clock64'})[0] == 200, 'coprime_clock_fixture')
            avi, mp4 = args.out / (str(number) + '.avi'), args.out / (str(number) + '.mp4')
            c.download('/api/media/download?path=' + quote(source, safe=''), avi)
            original = probe(avi)
            if args.pressure and number == 0:
                failures = c.state()['conversion']['failed']
                check(c.request('/api/codecs/test', 'POST', {'path': source, 'mode': 'queue_pressure'})[0] == 202, 'controlled_psram_pressure')
                pressure = wait_state(lambda s: s['conversion']['state'] == 'paused'
                                     and s['conversion']['error'] == 'psram_unavailable', 55)['conversion']
                check(pressure['enabled'] and pressure['failed'] == failures and pressure['frames'] == 0,
                      'resource_shortage_pauses_without_failure_protection')
                check(source in inventory(c), 'memory_shortage_preserves_source')
                check(c.request('/api/settings/transcode', 'POST', {'enabled': False})[0] == 200, 'stop_pressure_retry_for_standalone_test')
                report['pressure'] = pressure
            check(c.request('/api/codecs/test', 'POST', {'path': source})[0] == 202, 'convert_' + str(number))
            clip = {'width': width, 'clock64': clock64, 'runtime': []}
            report['clips'].append(clip)
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline:
                result = c.state('/api/codecs/test')
                state = c.state()
                clip['runtime'].append({k: state[k] for k in ('freeHeap', 'freePsram', 'temperatureC')})
                if not result['running']:
                    break
                time.sleep(.5)
            else:
                raise AssertionError('Short conversion exceeded deadline')
            clip['codec'] = result
            print('RESULT', json.dumps(result), flush=True)
            if args.red:
                check(result['error'] == 'mux_configuration_failed' and result['frames'] == result['totalFrames'], 'reproduced_clock_failure_after_all_frames')
            else:
                check(not result['error'] and result['frames'] == result['totalFrames'], 'conversion_complete_' + str(number))
                check(result['rawBufferBytes'] == width * (480 if width == 640 else 240) * 3 // 2
                      and result['blockBufferBytes'] == width * 16 * 2, 'bounded_pixel_buffers_' + str(number))
                c.download('/api/codecs/file', mp4)
                output = probe(mp4)
                decoded = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(mp4), '-f', 'null', '-'], capture_output=True, text=True)
                check(decoded.returncode == 0 and not decoded.stderr, 'whole_decode_' + str(number))
                av = next(x for x in original['streams'] if x['codec_type'] == 'video')
                mv = next(x for x in output['streams'] if x['codec_type'] == 'video')
                aa = next(x for x in original['streams'] if x['codec_type'] == 'audio')
                ma = next(x for x in output['streams'] if x['codec_type'] == 'audio')
                check((mv['width'], mv['height'], mv['nb_frames']) == (av['width'], av['height'], av['nb_frames']), 'dimensions_and_all_frames_' + str(number))
                check(Fraction(mv['avg_frame_rate']) == Fraction(av['avg_frame_rate']), 'exact_rational_clock_' + str(number))
                check(abs(float(mv['duration']) - float(av['duration'])) < .00001, 'exact_video_duration_' + str(number))
                check(ma['codec_name'] == 'aac' and ma['sample_rate'] == '16000' and abs(float(ma['duration']) - float(aa['nb_frames']) / 16000) < .13, 'audio_retained_' + str(number))
                similarity = subprocess.run(['ffmpeg', '-i', str(avi), '-i', str(mp4), '-lavfi', '[0:v]settb=AVTB,setpts=N*100000[a];[1:v]settb=AVTB,setpts=N*100000[b];[a][b]ssim', '-an', '-f', 'null', '-'], capture_output=True, text=True, check=True)
                import re
                match = re.search(r'All:([.\d]+)', similarity.stderr)
                clip['ssim'] = float(match[1]) if match else 0
                check(clip['ssim'] > .95, 'color_and_picture_similarity_' + str(number))
                clip['outputProbe'] = output
            check(source in inventory(c), 'standalone_test_retains_avi_' + str(number))
            c.request('/api/codecs/file', 'POST')
            check(c.request('/api/media/delete', 'POST', {'path': source})[0] == 200, 'cleanup_owned_' + str(number))
            owned.discard(source)
        report['passed'] = True
    finally:
        try:
            c.request('/api/codecs/file', 'POST')
            for path in owned:
                c.request('/api/media/delete', 'POST', {'path': path})
            if before is not None:
                check(inventory(c) == before, 'original_paths_and_sizes_preserved')
            report['restore'] = c.install(args.restore)
            if capture is not None:
                check(c.request('/api/settings/capture', 'POST', {'values': capture})[0] == 200, 'capture_settings_restored')
            if enabled is not None:
                check(c.request('/api/settings/transcode', 'POST', {'enabled': enabled})[0] == 200, 'conversion_setting_restored')
            check(c.request('/api/codecs/test')[0] == 404, 'fixture_routes_removed')
            state = c.state()
            check(state['cameraReady'] and not state['startupFailed'], 'normal_firmware_healthy')
        finally:
            c.clear()
            (args.out / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
