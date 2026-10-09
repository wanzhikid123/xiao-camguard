"""Real event-session/idle-conversion regression on an exact-source fixture.

Preserves every pre-existing media file. Scene substitutions affect only the
motion detector; the JPEG, PDM audio, SD writer and conversion worker are real.
Always restores the supplied production image, with conversion disabled until
the caller finishes the release checks.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import time
from urllib.parse import quote
from appliance_client import Client
from full_mode_tools import inventory, decode_avi

ROOT = Path(__file__).resolve().parents[2]


def frame_clocks(path):
    data = Path(path).read_bytes()
    assert data[:4] == b'RIFF' and data[8:12] == b'AVI '
    assert struct.unpack_from('<I', data, 4)[0] == len(data) - 8, 'AVI RIFF size must include the finalized index'
    at, clocks = 310, []
    while at + 8 <= len(data):
        kind, size = struct.unpack_from('<4sI', data, at)
        if kind == b'00dc':
            jpeg = data[at + 8:at + 8 + size]
            assert jpeg[:6] == b'\xff\xd8\xff\xef\x00\x1d'
            assert jpeg[6:14] == b'CGEVT002'
            clocks.append(jpeg[14:33].decode('ascii'))
        at += 8 + size + size % 2
        if kind == b'idx1':
            break
    return clocks


def clock_pixels(path):
    pixels = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(path), '-vf', 'crop=230:18:402:8,format=gray',
                                      '-frames:v', '1', '-f', 'rawvideo', '-'])
    return sum(x < 35 for x in pixels) > 2000 and sum(x > 210 for x in pixels) > 400


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', default='192.168.1.181')
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--restore', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    client = Client(args.address, ROOT / '.tools/private/admin-password.txt')
    report = {'passed': False, 'checks': [], 'physical_motion_tested': False,
              'credentials_or_tokens_saved': False, 'conversionSamples': []}
    old, settings, installed = None, None, False
    owned = set()

    def check(ok, name):
        report['checks'].append({'name': name, 'passed': bool(ok)})
        print('SESSION', 'PASS' if ok else 'FAIL', name, flush=True)
        if not ok:
            raise AssertionError(name)

    def wait(predicate, seconds=15):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            began = time.monotonic()
            state = client.state()
            if state['events']['armed']:
                report.setdefault('armedStatusSeconds', []).append(round(time.monotonic() - began, 3))
            if predicate(state):
                return state
            time.sleep(.25)
        report['lastWaitState'] = state
        raise AssertionError('State wait deadline')

    def scene(name):
        check(client.request('/api/events/test', 'POST', {'scene': name})[0] == 200, 'scene_' + name)

    def session(number, segments, brief_resume=False):
        before = inventory(client)
        scene('static')
        saved = client.state()['events']['savedClips']
        check(client.request('/api/events/start', 'POST', {'sensitivity': 5})[0] == 202, 'start_' + str(number))
        state = wait(lambda s: s['events']['armed'])
        check(state['fps'] == 15, 'monitoring_keeps_motion_sampling_rate_' + str(number))
        time.sleep(2.5)
        for i in range(segments):
            scene('motion')
            state = wait(lambda s: s['recording'])
            check(state['fps'] == 5, 'timestamp_recording_uses_feasible_cadence_' + str(number) + '_' + str(i))
            time.sleep(1.25)
            if brief_resume:
                scene('static')
                wait(lambda s: s['events']['quietPending'])
                time.sleep(.05)
                scene('motion')
                state = wait(lambda s: s['recording'] and not s['events']['quietPending'])
                check(state['events']['sessionEvents'] == 1, 'brief_quiet_resumes_same_event')
                time.sleep(1.25)
            scene('static')
            wait(lambda s: not s['recording'])
            check(client.state()['events']['savedClips'] == saved, 'no_intermediate_video_' + str(i))
            check(inventory(client) == before, 'quiet_period_has_no_finalized_video_' + str(i))
            if i + 1 < segments:
                time.sleep(3)
        began = time.monotonic()
        check(client.request('/api/events/stop', 'POST')[0] == 202, 'stop_' + str(number))
        report.setdefault('stopSeconds', []).append(round(time.monotonic() - began, 3))
        state = wait(lambda s: not s['events']['armed'] and not s['events']['requested'] and not s['recording'])
        after = inventory(client)
        new = set(after) - set(before)
        report.setdefault('finalizedSessions', []).append({'state': state, 'media': after, 'new': sorted(new)})
        print('FINALIZED', number, json.dumps({'events': state['events'], 'new': sorted(new)}), flush=True)
        check(len(new) == 1, 'one_video_per_monitoring_session_' + str(number))
        source = next(iter(new))
        check(source.endswith('_event_VGA_S.avi'), 'event_video_name_' + str(number))
        owned.add(source)
        check(state['events']['savedClips'] == saved + 1 and state['events']['sessionEvents'] == segments,
              'all_events_in_one_video_' + str(number))
        local = args.out / ('session-' + str(number) + '.avi')
        client.download('/api/media/download?path=' + quote(source, safe=''), local)
        validation = decode_avi(local)
        check(validation['passed'] and validation['whole_decode_passed'], 'whole_mjpeg_pcm_decode_' + str(number))
        check(validation['duration_difference_s'] < .25, 'audio_video_sync_' + str(number))
        check(clock_pixels(local), 'avi_has_visible_time_before_any_conversion_' + str(number))
        subprocess.run(['ffmpeg', '-v', 'error', '-i', str(local), '-frames:v', '1', '-y',
                        str(args.out / ('session-' + str(number) + '-avi-timestamp.png'))], check=True)
        clocks = frame_clocks(local)
        check(bool(clocks) and all(len(clock) == 19 for clock in clocks), 'every_frame_has_capture_clock_' + str(number))
        if segments > 1:
            from datetime import datetime
            span = (datetime.fromisoformat(clocks[-1]) - datetime.fromisoformat(clocks[0])).total_seconds()
            check(span > validation['durations_s']['video'] + 2, 'quiet_gap_removed_but_wall_clock_preserved')
        report.setdefault('sessions', []).append({'source': source, 'validation': validation, 'clocks': clocks})
        return source, clocks

    def convert(source, number):
        wait(lambda s: s['captureIdle'], 40)
        check(client.request('/api/codecs/test', 'POST', {'path': source, 'mode': 'queue'})[0] == 202,
              'queue_owned_idle_session_' + str(number))
        until = time.monotonic() + 240
        seen_running, last_print = False, 0
        while time.monotonic() < until:
            state = client.state()
            conversion = state['conversion']
            report['conversionSamples'].append({k: state[k] for k in ('conversion', 'freePsram', 'freeHeap', 'captureIdle')})
            if time.monotonic() - last_print > 15:
                print('CONVERT', number, conversion['state'], conversion['frames'], conversion['totalFrames'], conversion['error'], flush=True)
                last_print = time.monotonic()
            seen_running |= conversion['running']
            if conversion['source'] == source and not conversion['running'] and conversion['state'] == 'completed':
                break
            if conversion['source'] == source and not conversion['running'] and conversion['error'] not in ('', 'aborted'):
                raise AssertionError('Idle conversion failed: ' + conversion['error'])
            time.sleep(1)
        check(seen_running and conversion['source'] == source and conversion['state'] == 'completed',
              'idle_vga_h264_conversion_completed_' + str(number))
        check(client.request('/api/settings/transcode', 'POST', {'enabled': False})[0] == 200, 'pause_after_owned_conversion')
        published = source[:-4] + '.mp4'
        owned.add(published)
        media = inventory(client)
        check(source not in media and published in media, 'validated_mp4_replaces_avi_' + str(number))
        local = args.out / ('session-' + str(number) + '.mp4')
        client.download('/api/media/download?path=' + quote(published, safe=''), local)
        probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_streams', '-show_format', '-of', 'json', str(local)]))
        video = next(s for s in probe['streams'] if s['codec_type'] == 'video')
        audio = next(s for s in probe['streams'] if s['codec_type'] == 'audio')
        check(video['codec_name'] == 'h264' and video['width'] == 640 and video['height'] == 480 and audio['codec_name'] == 'aac',
              'vga_h264_aac_tracks_' + str(number))
        original = report['sessions'][number]
        check(int(video['nb_frames']) == len(original['clocks'])
              and abs(float(video['duration']) - original['validation']['durations_s']['video']) < .005,
              'mp4_preserves_event_frames_and_duration_' + str(number))
        check(abs(float(audio['duration']) - float(video['duration'])) < .25,
              'mp4_audio_video_sync_' + str(number))
        decode = subprocess.run(['ffmpeg', '-v', 'error', '-xerror', '-i', str(local), '-f', 'null', '-'], capture_output=True)
        check(decode.returncode == 0 and not decode.stderr, 'whole_h264_aac_decode_' + str(number))
        subprocess.run(['ffmpeg', '-v', 'error', '-i', str(local), '-frames:v', '1', '-y', str(args.out / ('session-' + str(number) + '-timestamp.png'))], check=True)
        # The exact top-right glyph mask is tested on-device; independently
        # require its dark panel/bright glyphs in the actual compressed MP4.
        check(clock_pixels(local),
              'timestamp_burned_into_top_right_pixels_' + str(number))
        report.setdefault('mp4', []).append({'path': published, 'probe': probe})

    try:
        client.login(progress=lambda p, s: print('LOGIN', p, s, flush=True))
        check(client.request('/api/settings/transcode', 'POST', {'enabled': False})[0] == 200, 'protect_historical_media')
        wait(lambda s: not s['conversion']['running'], 30)
        old = inventory(client)
        settings = client.state('/api/settings/capture')['values']
        report['baselineMedia'] = old
        report['baselineCapture'] = settings
        report['installation'] = client.install(args.firmware)
        installed = True
        capture = client.state('/api/settings/capture')
        check(all(x['width'] <= 640 and x['height'] <= 480 for x in capture['videoResolutions']), 'video_maximum_640x480')
        check(any(x['width'] == 2048 for x in capture['photoResolutions']), 'photo_resolution_retained')
        check(client.request('/api/settings/capture', 'POST', {'values': {'videoResolution': 11}})[0] == 400,
              'api_rejects_video_above_vga')
        check(client.request('/api/settings/capture', 'POST', {'values': {'idleSeconds': 30}}, timeout=30)[0] == 200, 'short_idle_policy')
        code, _, body = client.request('/api/events/test', 'POST', {'scene': 'timestamp_selftest'})
        check(code == 200 and json.loads(body) == {'passed': 10, 'total': 10}, 'timestamp_marker_and_overlay_selftests')
        source, _ = session(0, 2)
        # A completed ordinary recording previously retained a 320 KB AVI
        # index, enough to prevent the next VGA encoder initialization.
        before_manual = inventory(client)
        before_psram = client.state()['freePsram']
        check(client.request('/api/record/start', 'POST', timeout=30)[0] == 202, 'ordinary_recording_before_conversion')
        wait(lambda s: s['recording'])
        time.sleep(6)
        client.request('/api/record/stop', 'POST', timeout=30)
        wait(lambda s: not s['recording'] and not s['recordRequested'])
        manual_files = set(inventory(client)) - set(before_manual)
        check(len(manual_files) == 1 and next(iter(manual_files)).endswith('.avi'), 'owned_ordinary_recording_created')
        owned.update(manual_files)
        check(client.state()['freePsram'] > before_psram - 65536, 'completed_recording_releases_avi_index')
        convert(source, 0)
        # Repeat after full sensor restoration; this catches failures hidden
        # by a fresh reboot or by testing just one codec initialization.
        source, _ = session(1, 1, brief_resume=True)
        convert(source, 1)
        before = inventory(client)
        scene('static')
        client.request('/api/events/start', 'POST', {'sensitivity': 5})
        wait(lambda s: s['events']['armed'])
        time.sleep(3)
        client.request('/api/events/stop', 'POST')
        wait(lambda s: not s['events']['armed'])
        check(inventory(client) == before, 'empty_monitoring_session_creates_no_video')
        check(all(inventory(client).get(path) == size for path, size in old.items()), 'all_original_media_preserved')
        report['passed'] = True
    except Exception as failure:
        report['error'] = str(failure)
        print('SESSION FAIL', str(failure), flush=True)
    finally:
        if installed:
            try:
                client.request('/api/events/stop', 'POST')
                wait(lambda s: not s['events']['armed'] and not s['recording'])
                client.request('/api/settings/transcode', 'POST', {'enabled': False})
                for path in owned:
                    client.request('/api/media/delete', 'POST', {'path': path})
                if settings:
                    client.request('/api/settings/capture', 'POST', {'values': settings}, timeout=30)
                report['restore'] = client.install(args.restore)
                check(client.request('/api/events/test', 'POST')[0] == 404, 'production_has_no_scene_fixture')
                check(client.request('/api/codecs/test')[0] == 404, 'production_has_no_codec_fixture')
                check(inventory(client) == old, 'exact_original_inventory_after_cleanup')
            except Exception as failure:
                report['passed'] = False
                report['restoreError'] = str(failure)
                print('RESTORE FAIL', str(failure), flush=True)
        (args.out / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf8')
        client.clear()
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
