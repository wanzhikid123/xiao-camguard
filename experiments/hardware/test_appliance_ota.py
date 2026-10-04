"""Real-account OTA gate: reject unsafe uploads, retain slot on failure, then switch twice.

Uses ignored private password and generated product image. No credentials,
cookies or CSRF are persisted. USB installation of the new OTA receiver must
be completed first. The valid image is installed into both existing app slots.
"""
import argparse
import hashlib
import http.client
import json
from pathlib import Path
import socket
import threading
import time
from urllib.error import HTTPError
from urllib.request import Request
import baseline_http as transport

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True)
    parser.add_argument('--firmware', type=Path, default=ROOT / '.pio/build/xiao-appliance/firmware.bin')
    parser.add_argument('--password-file', type=Path, default=ROOT / '.tools/private/admin-password.txt')
    parser.add_argument('--out', type=Path, default=ROOT / '.tools/hardware/appliance-ota')
    parser.add_argument('--preflight-only', action='store_true')
    parser.add_argument('--without-boot-health', action='store_true', help='Only for the archived first receiver build')
    parser.add_argument('--rollback-firmware', type=Path, help='Explicit known-board boot-health rejection fixture')
    parser.add_argument('--install-first', action='store_true', help='Upgrade the archived first receiver before running the gate')
    args = parser.parse_args()
    socket.inet_aton(args.address)
    args.out.mkdir(parents=True, exist_ok=True)
    password = args.password_file.read_text(encoding='utf-8').rstrip('\r\n')
    image = args.firmware.read_bytes()
    digest = hashlib.sha256(image).hexdigest()
    base = 'http://' + args.address
    cookie = csrf = ''
    report = dict(passed=False, checks=[], credentials_or_tokens_saved=False,
                  firmware_bytes=len(image), firmware_sha256=digest, slot_transitions=[])

    def expect(ok, name):
        report['checks'].append(dict(name=name, passed=bool(ok)))
        print(f"OTA {'PASS' if ok else 'FAIL'} {name}", flush=True)
        if not ok: raise RuntimeError('OTA check failed: ' + name)

    def request(path, method='GET', value=None, raw=None, authorized=True, headers=None, timeout=10):
        supplied = {'Cookie': cookie} if authorized and cookie else {}
        if method == 'POST': supplied.update({'X-CSRF-Token': csrf, 'Origin': base})
        if value is not None:
            raw = json.dumps(value).encode(); supplied['Content-Type'] = 'application/json'
        supplied.update(headers or {})
        try:
            response = transport.HTTP.open(Request(base+path, data=raw, method=method, headers=supplied), timeout=timeout)
        except HTTPError as error: response = error
        with response:
            body = response.read(200000)
            if password.encode() in body: raise RuntimeError('Private password in response')
            return response.code, dict(response.headers), body

    def state(path='/api/status'):
        code, _, body = request(path)
        if code != 200: raise RuntimeError('State unavailable: '+path)
        return json.loads(body)

    def login():
        nonlocal cookie, csrf
        cookie = csrf = ''
        code, headers, body = request('/api/auth/login', 'POST', {'password': password}, headers={'X-Appliance-Request':'1'})
        if code == 429:
            time.sleep(min(61, int(headers.get('Retry-After','60'))+1))
            code, _, body = request('/api/auth/login', 'POST', {'password': password}, headers={'X-Appliance-Request':'1'})
        expect(code == 202, 'chosen_account_login_accepted')
        ticket = json.loads(body)['ticket']; deadline = time.monotonic()+120
        while time.monotonic() < deadline:
            code, headers, body = request('/api/auth/result', headers={'X-Login-Ticket':ticket})
            if code == 202: time.sleep(.7); continue
            expect(code == 200, 'chosen_account_login_verified')
            cookie = headers['Set-Cookie'].split(';',1)[0]; csrf = json.loads(body)['csrf']; return
        raise RuntimeError('Login timeout')

    def wait_boot():
        deadline = time.monotonic()+70
        time.sleep(3)
        while time.monotonic() < deadline:
            try:
                code, _, body = request('/api/bootstrap', authorized=False, timeout=3)
                if code == 200 and json.loads(body)['adminConfigured']:
                    time.sleep(3); return
            except (OSError, ValueError, http.client.HTTPException): pass
            time.sleep(.5)
        raise RuntimeError('Reboot did not recover HTTP')

    def upload(payload, sha=digest, headers=None, authorized=True, path='/api/ota'):
        fields = {'Content-Type':'application/octet-stream', 'X-Firmware-SHA256':sha}
        fields.update(headers or {})
        return request(path, 'POST', raw=payload, headers=fields, authorized=authorized, timeout=100)

    class Preview:
        def __init__(self):
            self.cookie = cookie; self.ready = threading.Event(); self.done = threading.Event(); self.bytes = 0
            self.thread = threading.Thread(target=self.run, daemon=True)
        def run(self):
            try:
                with transport.HTTP.open(Request(base+'/api/camera/stream', headers={'Cookie':self.cookie}), timeout=8) as response:
                    while chunk := response.read(4096):
                        self.bytes += len(chunk); self.ready.set()
            except (OSError, http.client.HTTPException): pass
            finally: self.cookie = ''; self.done.set()

    try:
        expect(request('/api/ota', authorized=False)[0] == 401, 'ota_status_requires_login')
        expect(upload(b'\0'*288, authorized=False)[0] == 403, 'ota_upload_requires_login')
        login()
        initial = state()
        if not initial['cameraReady']:
            expect(request('/api/device/restart','POST')[0] == 202, 'normal_recovery_restart')
            wait_boot(); login()
        expect(state()['cameraReady'] and state()['sdReady'], 'camera_sd_ready_before_ota')
        if args.install_first:
            code,_,body=upload(image)
            expect(code==200 and json.loads(body)['updated'], 'install_final_receiver_over_authenticated_ota')
            wait_boot(); login()
        old_slot = state('/api/ota')['runningSlot']; report['initial_slot'] = old_slot
        expect(state('/api/ota')['maximumBytes'] == 2673868, 'ota_slot_eighty_percent_limit')
        expect(state('/api/ota')['automaticBootRollback'] != args.without_boot_health, 'boot_rollback_capability_matches_profile')
        expect(upload(image[:288], headers={'X-CSRF-Token':''})[0] == 403, 'ota_csrf_required')
        expect(upload(image[:288], headers={'Origin':'http://untrusted.invalid'})[0] == 403, 'ota_origin_required')
        expect(upload(image[:288], path='/api/ota?ignored=1')[0] == 400, 'ota_query_rejected')
        expect(upload(image[:288], headers={'Content-Type':'application/json'})[0] == 400, 'ota_exact_content_type')
        expect(upload(image[:288], sha='')[0] == 400, 'ota_hash_required')
        expect(upload(image[:288], sha='g'*64)[0] == 400, 'ota_hash_hex_validation')
        expect(upload(image[:287])[0] == 400, 'ota_short_image_rejected')
        oversized = http.client.HTTPConnection(args.address, timeout=5)
        oversized.putrequest('POST','/api/ota')
        for key,value in {'Content-Length':'2673869','Content-Type':'application/octet-stream','Cookie':cookie,
                           'X-CSRF-Token':csrf,'Origin':base,'X-Firmware-SHA256':digest}.items(): oversized.putheader(key,value)
        oversized.endheaders(); oversized.send(image[:288])
        answer=oversized.getresponse(); expect(answer.status==400, 'oversize_rejected_before_body_or_flash'); answer.read(); oversized.close()
        expect(upload(b'\0'*288)[0] == 400, 'ota_bad_magic_rejected')
        wrong_chip = bytearray(image[:288]); wrong_chip[12:14] = b'\0\0'
        expect(upload(wrong_chip)[0] == 400, 'ota_wrong_chip_rejected_before_flash')
        expect(state()['cameraReady'] and not state()['maintenance'], 'preflight_errors_keep_camera_running')
        expect(state('/api/ota')['runningSlot'] == old_slot, 'preflight_errors_keep_running_slot')
        expect(request('/update','POST')[0] == 404 and request('/upload','POST')[0] == 404, 'legacy_ota_routes_absent')
        if args.preflight_only:
            report['passed'] = True; return 0

        corrupt = bytearray(image); corrupt[4096] ^= 1
        code, _, body = upload(corrupt)
        expect(code == 400 and json.loads(body)['error'] == 'sha256_mismatch', 'complete_corrupt_upload_fails_sha256')
        expect(json.loads(body)['restarting'], 'failed_flash_upload_recovers_by_restart')
        wait_boot(); login()
        expect(state('/api/ota')['runningSlot'] == old_slot, 'corrupt_upload_does_not_activate_slot')
        expect(state()['cameraReady'] and state()['sdReady'], 'camera_sd_recover_after_corrupt_upload')

        # Deliberately stop delivering the payload while the async receiver waits.
        # Other HTTP requests must remain usable; logout revokes this transfer.
        connection = http.client.HTTPConnection(args.address, timeout=10)
        connection.putrequest('POST','/api/ota')
        for key, value in {'Content-Length':str(len(image)), 'Content-Type':'application/octet-stream',
                           'Cookie':cookie, 'X-CSRF-Token':csrf, 'Origin':base,
                           'X-Firmware-SHA256':digest}.items(): connection.putheader(key,value)
        connection.endheaders(); connection.send(image[:4096])
        deadline = time.monotonic()+5
        while time.monotonic() < deadline and state('/api/ota')['state'] != 'uploading': time.sleep(.1)
        expect(state('/api/ota')['state'] == 'uploading', 'async_slow_upload_started')
        started = time.monotonic()
        expect(state()['maintenance'], 'status_responsive_during_upload')
        report['upload_status_latency_s'] = round(time.monotonic()-started,3)
        for path in ('/api/record/start','/api/camera/photo','/api/device/restart','/api/network/cancel'):
            expect(request(path,'POST')[0] == 409, 'maintenance_blocks_'+path.rsplit('/',1)[-1])
        expect(request('/api/auth/logout','POST')[0] == 200, 'logout_available_during_upload')
        try:
            connection.send(image[4096:8192]); response = connection.getresponse(); body = response.read(2000)
            expect(response.status == 400 and json.loads(body)['error'] == 'session_revoked', 'existing_upload_aborted_on_logout')
        finally: connection.close()
        wait_boot(); login()
        expect(state('/api/ota')['runningSlot'] == old_slot, 'revoked_upload_does_not_activate_slot')
        expect(state()['cameraReady'], 'camera_recovers_after_revoked_upload')

        for number in (1,2):
            before = state('/api/ota'); preview = Preview(); preview.thread.start()
            expect(preview.ready.wait(6), 'preview_active_before_valid_update')
            code, _, body = upload(image)
            expect(code == 200 and json.loads(body)['updated'], 'authenticated_valid_update_'+str(number))
            expect(preview.done.wait(2), 'existing_preview_completed_before_task_shutdown')
            wait_boot(); login()
            after = state('/api/ota')
            expect(after['runningSlot'] == before['targetSlot'] and after['runningSlot'] != before['runningSlot'], 'actual_dual_slot_transition_'+str(number))
            report['slot_transitions'].append({'from':before['runningSlot'],'to':after['runningSlot']})
            expect(state()['cameraReady'] and state()['sdReady'] and state()['audioEnabled'], 'camera_sd_pdm_recover_after_update_'+str(number))
            code, _, body = request('/api/camera/still')
            expect(code == 200 and body.startswith(b'\xff\xd8') and body.endswith(b'\xff\xd9'), 'jpeg_after_update_'+str(number))
            (args.out/f'post-ota-{number}.jpg').write_bytes(body)
        expect(state('/api/ota')['runningSlot'] == old_slot, 'two_updates_return_to_initial_slot')
        if args.rollback_firmware:
            fixture=args.rollback_firmware.read_bytes()
            code,_,body=upload(fixture,sha=hashlib.sha256(fixture).hexdigest())
            expect(code==200 and json.loads(body)['updated'], 'boot_health_fixture_installed_by_authenticated_ota')
            # Let the fixture reject itself and return to the prior valid app.
            time.sleep(23); wait_boot(); login()
            expect(state('/api/ota')['runningSlot']==old_slot, 'failed_boot_health_rolls_back_to_previous_valid_slot')
            expect(state()['cameraReady'] and state()['sdReady'] and state()['configReady'], 'rollback_restores_camera_sd_existing_configuration')
            report['boot_health_rollback_passed']=True
        expect(request('/api/auth/logout','POST')[0] == 200, 'final_logout')
        report['passed'] = True
    except Exception as error:
        report['error'] = type(error).__name__+': '+str(error)
        print(report['error'], flush=True)
    finally:
        cookie = csrf = password = ''
        report['check_count'] = len(report['checks'])
        (args.out/'report.json').write_text(json.dumps(report,indent=2), encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__': raise SystemExit(main())
