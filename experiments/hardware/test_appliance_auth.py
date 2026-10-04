"""Camera/auth integration on the explicit synthetic RAM fixture by default.

Bounded temporary Windows AP visit; restores the user's existing profile.
Never enrolls or persists passwords, cookies, login tickets or CSRF tokens.
Leaves a twenty-second AVI on the SD for independent validation after restore.
--locked checks the unconfigured production build without enrolling an account.
"""
import argparse
import json
from pathlib import Path
import re
import threading
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request
from xml.sax.saxutils import escape
import baseline_http as transport

ROOT = Path(__file__).resolve().parents[2]
BASE = "http://192.168.4.1"
PROFILE = "Codex-XIAO-camera-auth-test"
SSID = "XIAO-Camera-Test-084C"
PASSWORD = "synthetic-admin-password"
OUT = ROOT / ".tools/hardware/appliance-auth"
CONNECT_RETRIES = 0
STATUS_FIELDS = {
    "cameraReady", "startupFailed", "recording", "recordRequested", "fps", "audioEnabled",
    "temperatureC", "uptimeSeconds", "freeHeap", "freePsram", "totalPsram", "sdReady",
    "sdTotalBytes", "sdFreeBytes", "wifiConnected", "lanIP", "rssi", "configReady",
    "configRevision", "wireguardConfigured", "wireguardRunning",
}


def request(path, *, method="GET", headers=None, data=None):
    global CONNECT_RETRIES
    req = Request(BASE + path, data=data, headers=headers or {}, method=method)
    started = time.monotonic()
    for attempt in range(4):
        try:
            response = transport.HTTP.open(req, timeout=8)
        except HTTPError as error:
            response = error
        except URLError as error:
            # These connect errors occur before a request is sent. Do not
            # retry a mutation or a response that could already be consumed.
            if method == "GET" and getattr(error.reason, "winerror", 0) in (10051, 10065) and attempt < 3:
                CONNECT_RETRIES += 1
                time.sleep(.25)
                continue
            raise
        break
    with response:
        payload = response.read(250000)
        return response.code, dict(response.headers), payload, time.monotonic() - started


class Stream:
    def __init__(self, cookie):
        self.cookie = cookie
        self.ready, self.done = threading.Event(), threading.Event()
        self.frames, self.bytes, self.ended, self.error = 0, 0, 0, None
        self.thread = threading.Thread(target=self.run, daemon=True)

    def run(self):
        try:
            req = Request(BASE + "/api/camera/stream", headers={"Cookie": self.cookie})
            with transport.HTTP.open(req, timeout=5) as response:
                if response.code != 200 or "multipart/x-mixed-replace" not in response.headers.get("Content-Type", ""):
                    raise RuntimeError("Invalid preview response")
                pending = b""
                deadline = time.monotonic() + 180
                while time.monotonic() < deadline:
                    chunk = response.read(4096)
                    if not chunk:
                        break
                    self.bytes += len(chunk)
                    pending += chunk
                    while True:
                        start, end = pending.find(b"\xff\xd8"), pending.find(b"\xff\xd9")
                        if start < 0 or end < start:
                            break
                        frame = pending[start:end + 2]
                        pending = pending[end + 2:]
                        if self.frames < 3:
                            (OUT / f"preview-{self.frames}.jpg").write_bytes(frame)
                        self.frames += 1
                        self.ready.set()
                    if len(pending) > 250000:
                        raise RuntimeError("Unbounded/incomplete preview frame")
                else:
                    raise RuntimeError("Preview exceeded bounded test duration")
        except Exception as error:
            self.error = type(error).__name__
        finally:
            self.cookie = ""
            self.ended = time.monotonic()
            self.done.set()


def main():
    global PROFILE, SSID, OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--restore-profile", required=True)
    parser.add_argument("--interface", default="WLAN")
    parser.add_argument("--locked", action="store_true", help="Check the unconfigured xiao-appliance build without creating an account")
    args = parser.parse_args()
    if args.locked:
        PROFILE = "Codex-XIAO-camera-locked-test"
        SSID = "ESP-CAM_MJPEG_084CFBA172E0"
        OUT = ROOT / ".tools/hardware/appliance-locked"
    OUT.mkdir(parents=True, exist_ok=True)
    profile_path = OUT / "temporary-ap.xml"
    if PROFILE in transport.netsh("show", "profiles"):
        raise SystemExit("Temporary integration profile already exists; inspect before reuse")
    profile_path.write_text(f'''<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
<name>{PROFILE}</name><SSIDConfig><SSID><name>{escape(SSID)}</name></SSID></SSIDConfig>
<connectionType>ESS</connectionType><connectionMode>manual</connectionMode>
<MSM><security><authEncryption><authentication>open</authentication><encryption>none</encryption><useOneX>false</useOneX></authEncryption></security></MSM>
</WLANProfile>''', encoding="utf-8")
    checks, latencies, logins = [], [], []
    report = {"passed": False, "synthetic_ram_fixture": not args.locked, "checks": checks,
              "passwords_or_tokens_saved": False, "production_enrollment": False}
    cookies, stream = [], None
    added = False

    def expect(condition, name):
        checks.append({"name": name, "passed": bool(condition)})
        print(f"CAMERA AUTH {'PASS' if condition else 'FAIL'} {name}", flush=True)
        if not condition:
            raise RuntimeError(f"Camera authentication check failed: {name}")

    def login_start():
        code, _, body, _ = request("/api/auth/login", method="POST",
            data=json.dumps({"password": PASSWORD}).encode(),
            headers={"Content-Type": "application/json", "X-Appliance-Request": "1"})
        expect(code == 202, "async_login_accepted")
        return json.loads(body)["ticket"]

    def login_poll(ticket, concurrent=None):
        started = time.monotonic()
        while time.monotonic() - started < 150:
            code, headers, body, _ = request("/api/auth/result", headers={"X-Login-Ticket": ticket})
            if code != 202:
                expect(code == 200, "login_completes")
                logins.append(round(time.monotonic() - started, 3))
                cookie = headers["Set-Cookie"].split(";", 1)[0]
                token = json.loads(body)["csrf"]
                cookies.append((cookie, token))
                return cookie, token
            code, _, _, latency = request("/")
            expect(code == 200, "embedded_page_available_during_login") if not latencies else None
            if code != 200:
                raise RuntimeError("Embedded page blocked during login")
            latencies.append(latency)
            if concurrent:
                concurrent(time.monotonic() - started)
            time.sleep(.75)
        raise RuntimeError("Bounded password work timed out")

    def status(cookie):
        code, _, body, latency = request("/api/status", headers={"Cookie": cookie})
        if code != 200:
            raise RuntimeError("Authenticated status unavailable")
        latencies.append(latency)
        return json.loads(body)

    def mutate(path, cookie, csrf):
        return request(path, method="POST", headers={"Cookie": cookie, "X-CSRF-Token": csrf, "Origin": BASE})

    try:
        transport.netsh("add", "profile", f"filename={profile_path}", "user=current", f"interface={args.interface}")
        added = True
        transport.netsh("connect", f"name={PROFILE}", f"interface={args.interface}")
        transport.PROFILE = PROFILE
        page = transport.wait_http(45, reconnect_interface=args.interface)
        expect(b'lang="zh-CN"' in page and b'/app.js' in page, "embedded_offline_login_page")
        expect(b'https://' not in page and b'http://' not in page, "page_has_no_external_dependencies")
        for path, source in (("/app.js", "app.js"), ("/style.css", "style.css")):
            code, headers, payload, _ = request(path)
            expected = (ROOT / "ui/appliance" / source).read_text(encoding="utf-8").encode()
            expect(code == 200 and payload == expected, "embedded_asset_matches_" + source)
            expect("Content-Security-Policy" in headers and "Access-Control-Allow-Origin" not in headers,
                   "asset_security_headers_" + source)
        expect(json.loads(request("/api/bootstrap")[2]) == {"adminConfigured": not args.locked},
               "account_readiness_matches_build")
        for path in ("/api/status", "/api/camera/still", "/api/camera/stream"):
            expect(request(path)[0] == 401, "anonymous_denied_" + path)
            expect(request(path, headers={"Authorization": "Basic YWRtaW46YWRtaW4="})[0] == 401,
                   "basic_denied_" + path)
        for path, method in (("/control?still=1", "GET"), ("/control?forceRecord=1", "GET"),
                             ("/status", "GET"), ("/web?configs.txt", "GET"), ("/wifi", "GET"),
                             ("/sustain?stream=1", "GET"), ("/ws", "GET"), ("/sse", "GET"),
                             ("/upload", "POST"), ("/update", "POST")):
            expect(request(path, method=method)[0] == 404, "legacy_route_absent_" + path)
        for path in ("/api/record/start", "/api/record/stop", "/api/device/restart"):
            expect(request(path, method="POST")[0] == 403, "anonymous_mutation_denied_" + path)
        for value, name in ((b'{"password":{"value":"synthetic-admin-password"}}', "nested_object_rejected"),
                            (b'{"password":' + b'[' * 200 + b'"x"' + b']' * 200 + b'}', "deep_array_rejected"),
                            (b'"synthetic-admin-password"', "root_string_rejected"),
                            (b'{"password":"synthetic-admin-password\\u0000hidden"}', "decoded_nul_escape_rejected")):
            expect(request("/api/auth/login", method="POST", data=value,
                headers={"Content-Type": "application/json", "X-Appliance-Request": "1"})[0] == 400, name)
        if args.locked:
            code, _, body, _ = request("/api/auth/login", method="POST",
                data=json.dumps({"password": PASSWORD}).encode(),
                headers={"Content-Type": "application/json", "X-Appliance-Request": "1"})
            expect(code == 409 and json.loads(body).get("error") == "admin_not_configured",
                   "unconfigured_account_cannot_login")
            code, _, _, _ = request("/api/auth/login", method="POST",
                data=b'{"password":"literal-[brackets]-password"}',
                headers={"Content-Type": "application/json", "X-Appliance-Request": "1"})
            expect(code == 409, "literal_brackets_in_password_are_valid_json")
            code, _, _, _ = request("/api/auth/login", method="POST",
                data=json.dumps({"password": r"literal-\u0000-password"}).encode(),
                headers={"Content-Type": "application/json", "X-Appliance-Request": "1"})
            expect(code == 409, "literal_backslash_unicode_text_is_valid_json")
            report.update(passed=True, check_count=len(checks), unconfigured_fail_closed=True)
            return 0
        cookie, csrf = login_poll(login_start())
        state = status(cookie)
        if not state["cameraReady"]:
            # USB reset can hit the upstream crash latch. Use the authenticated
            # normal restart, never raw RTC/NVS/flash edits.
            expect(mutate("/api/device/restart", cookie, csrf)[0] == 202, "authenticated_recovery_restart")
            time.sleep(3)
            transport.wait_http(45, reconnect_interface=args.interface)
            cookie, csrf = login_poll(login_start())
            state = status(cookie)
        expect(state["cameraReady"] and state["sdReady"], "camera_and_sd_ready")
        expect(state["fps"] == 15 and state["audioEnabled"], "v1_defaults_fps_audio")
        expect(set(state) == STATUS_FIELDS, "status_uses_explicit_safe_allowlist")
        for path in ("/api/record/start", "/api/record/stop", "/api/device/restart"):
            expect(request(path, method="GET", headers={"Cookie": cookie})[0] in (404, 405),
                   "get_cannot_mutate_" + path)
        expect(request("/api/record/start", method="POST", headers={"Cookie": cookie})[0] == 403,
               "recording_requires_csrf")
        expect(request("/api/record/start", method="POST", headers={"Cookie": cookie, "X-CSRF-Token": csrf,
                       "Origin": "http://evil.invalid"})[0] == 403, "recording_requires_same_origin")
        expect(not status(cookie)["recordRequested"], "rejected_controls_do_not_record")
        expect(mutate("/api/record/start?ignored=1", cookie, csrf)[0] == 400,
               "unexpected_control_query_rejected")
        expect(mutate("/api/record/start?", cookie, csrf)[0] == 400,
               "empty_control_query_rejected")
        expect(not status(cookie)["recordRequested"], "rejected_query_does_not_record")
        code, headers, image, _ = request("/api/camera/still", headers={"Cookie": cookie})
        expect(code == 200 and image[:2] == b'\xff\xd8' and image[-2:] == b'\xff\xd9', "authenticated_still_jpeg")
        expect("Access-Control-Allow-Origin" not in headers, "still_has_no_permissive_cors")
        (OUT / "still.jpg").write_bytes(image)
        report["still_bytes"] = len(image)
        stream = Stream(cookie)
        stream.thread.start()
        expect(stream.ready.wait(8), "authenticated_live_stream_starts")
        expect(mutate("/api/record/start", cookie, csrf)[0] == 202, "authorized_record_start")
        recording_start = time.monotonic()
        stopped = False
        recording_observed = False

        def concurrent(elapsed):
            nonlocal stopped, recording_observed
            state = status(cookie)
            recording_observed |= state["recording"]
            if time.monotonic() - recording_start >= 20 and not stopped:
                expect(mutate("/api/device/restart", cookie, csrf)[0] == 409, "restart_refused_during_recording")
                expect(mutate("/api/record/stop", cookie, csrf)[0] == 202, "authorized_record_stop_during_kdf")
                stopped = True
            if stream.done.is_set():
                raise RuntimeError("Live stream stopped during password work")

        frames_before = stream.frames
        second_cookie, second_csrf = login_poll(login_start(), concurrent)
        expect(stopped and recording_observed, "recording_runs_during_password_work")
        expect(stream.frames - frames_before >= 30, "live_preview_runs_during_password_work")
        until = time.monotonic() + 10
        while status(cookie)["recording"] and time.monotonic() < until:
            time.sleep(.25)
        expect(not status(cookie)["recording"], "recording_finalized")
        logout_at = time.monotonic()
        expect(mutate("/api/auth/logout", cookie, csrf)[0] == 200, "logout_accepted")
        expect(stream.done.wait(6) and stream.error is None, "logout_closes_existing_stream")
        report["stream_close_after_logout_seconds"] = round(stream.ended - logout_at, 3)
        report["stream_frames"] = stream.frames
        report["stream_bytes"] = stream.bytes
        for path in ("/api/status", "/api/camera/still", "/api/camera/stream"):
            expect(request(path, headers={"Cookie": cookie})[0] == 401, "logged_out_denied_" + path)
        expect(status(second_cookie)["cameraReady"], "logout_preserves_other_client_session")
        expect(mutate("/api/auth/logout", second_cookie, second_csrf)[0] == 200, "second_client_logout")
        expect(latencies and max(latencies) < 2, "camera_http_responsive_during_kdf")
        report.update(passed=True, check_count=len(checks), login_seconds=logins,
                      http_probes=len(latencies), max_http_seconds=round(max(latencies), 3))
    finally:
        for cookie, csrf in cookies:
            try:
                mutate("/api/record/stop", cookie, csrf)
                mutate("/api/auth/logout", cookie, csrf)
            except OSError:
                pass
        if stream:
            stream.thread.join(timeout=7)
        if added:
            transport.netsh("connect", f"name={args.restore_profile}", f"interface={args.interface}", check=False)
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                if re.search(r"SSID\s*:\s*" + re.escape(args.restore_profile) + r"\s*$",
                             transport.netsh("show", "interfaces", check=False), re.M):
                    report["original_wifi_restored"] = True
                    break
                time.sleep(.5)
            transport.netsh("delete", "profile", f"name={PROFILE}", f"interface={args.interface}", check=False)
            if not report.get("original_wifi_restored"):
                report["passed"] = False
        profile_path.unlink(missing_ok=True)
        report["connect_retries"] = CONNECT_RETRIES
        (OUT / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({k: v for k, v in report.items() if k != "checks"}), flush=True)
        if added and not report.get("original_wifi_restored"):
            raise RuntimeError("Original Windows Wi-Fi profile was not restored")
    return 0 if report["passed"] and report.get("original_wifi_restored") else 1


if __name__ == "__main__":
    raise SystemExit(main())
