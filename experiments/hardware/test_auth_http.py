"""Real isolated HTTP auth checks; synthetic credentials, redacted report.

Creates only a temporary open-AP profile and restores the specified existing
Windows profile in finally. Never reads/saves real credentials or session tokens.
"""
import argparse
import json
from pathlib import Path
import time
from urllib.error import HTTPError
from urllib.request import Request
from xml.sax.saxutils import escape
import baseline_http as transport

PROFILE = "Codex-XIAO-auth-test"
PASSWORD = "synthetic-admin-password"
BASE = "http://192.168.4.1"
ROOT = Path(__file__).resolve().parents[2]


def exchange(path, body=None, headers=None, raw=None):
    data = raw if raw is not None else json.dumps(body).encode() if body is not None else None
    merged = {"Content-Type": "application/json", "X-Appliance-Request": "1"}
    if headers:
        merged.update(headers)
    req = Request(BASE + path, data=data, headers=merged)
    started = time.monotonic()
    try:
        response = transport.HTTP.open(req, timeout=4)
    except HTTPError as error:
        response = error
    with response:
        payload = response.read(2048)
        status, metadata = response.code, dict(response.headers)
    try:
        parsed = json.loads(payload)
    except ValueError:
        parsed = None
    return status, metadata, parsed, time.monotonic() - started


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--restore-profile", required=True)
    parser.add_argument("--interface", default="WLAN")
    args = parser.parse_args()
    out = ROOT / ".tools/hardware/auth-http"
    out.mkdir(parents=True, exist_ok=True)
    profile_path = out / "temporary-ap.xml"
    if PROFILE in transport.netsh("show", "profiles"):
        raise SystemExit("Temporary auth profile already exists; inspect before reuse")
    profile_path.write_text(f'''<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
<name>{PROFILE}</name><SSIDConfig><SSID><name>{escape('XIAO-Auth-Test-084C')}</name></SSID></SSIDConfig>
<connectionType>ESS</connectionType><connectionMode>manual</connectionMode>
<MSM><security><authEncryption><authentication>open</authentication><encryption>none</encryption><useOneX>false</useOneX></authEncryption></security></MSM>
</WLANProfile>''', encoding="utf-8")
    checks, responsiveness, logins = [], [], []
    report = {"fixture": True, "passed": False, "checks": checks,
              "product_auth_acceptance": "pending_integration", "passwords_or_tokens_saved": False}

    def expect(condition, name):
        checks.append({"name": name, "passed": bool(condition)})
        print(f"AUTH HTTP {'PASS' if condition else 'FAIL'} {name}", flush=True)
        if not condition:
            raise RuntimeError(f"Authentication fixture check failed: {name}")

    def login(password, completed=True):
        status, _, result, _ = exchange("/api/auth/login", {"password": password})
        expect(status == 202 and len(result.get("ticket", "")) == 64, "login_accepted_asynchronously")
        ticket = result["ticket"]
        expect(exchange("/api/auth/result", headers={"X-Login-Ticket": "0" * 64})[0] == 401,
               "wrong_ticket_denied")
        return poll(ticket) if completed else ticket

    def poll(ticket):
        started = time.monotonic()
        while time.monotonic() - started < 150:
            status, headers, body, _ = exchange("/api/auth/result", headers={"X-Login-Ticket": ticket})
            if status != 202:
                logins.append(round(time.monotonic() - started, 3))
                return status, headers, body
            # A public HTTP page must stay available during expensive hashing.
            page_status, _, _, latency = exchange("/")
            if page_status != 200:
                raise RuntimeError("HTTP blocked during password work")
            responsiveness.append(latency)
            time.sleep(0.75)
        raise RuntimeError("Bounded asynchronous login timed out")

    added = False
    try:
        transport.netsh("add", "profile", f"filename={profile_path}", "user=current", f"interface={args.interface}")
        added = True
        transport.netsh("connect", f"name={PROFILE}", f"interface={args.interface}")
        transport.PROFILE = PROFILE
        transport.wait_http(45, reconnect_interface=args.interface)
        expect(exchange("/api/test/status")[0] == 401, "unauthenticated_status_denied")
        expect(exchange("/api/test/status", headers={"Authorization": "Basic YWRtaW46YWRtaW4="})[0] == 401,
               "basic_auth_does_not_bypass_session")
        expect(exchange("/api/auth/login", {"password": PASSWORD}, {"Origin": "http://evil.invalid"})[0] == 403,
               "cross_origin_login_denied")
        expect(exchange("/api/auth/login", {"password": PASSWORD}, {"X-Appliance-Request": ""})[0] == 403,
               "login_requires_custom_request_header")
        expect(exchange("/api/auth/login", raw=b'{"password":"test"} trailing')[0] == 400,
               "trailing_json_rejected")
        expect(exchange("/api/auth/login", raw=b'{"password":"one","password":"two"}')[0] == 400,
               "duplicate_password_rejected")
        expect(exchange("/api/auth/login", raw=b'{"password":"synthetic-admin-password\\u0000hidden"}')[0] == 400,
               "embedded_nul_escape_rejected")
        expect(exchange("/api/auth/login", raw=b"x" * 513)[0] == 400, "request_size_bounded")
        expect(exchange("/api/auth/login", {"password": PASSWORD}, {"Content-Type": "application/jsonhack"})[0] == 400,
               "invalid_content_type_denied")
        expect(login("short")[0] == 401, "password_policy_failure_is_generic")
        ticket = login("incorrect-admin-password", completed=False)
        expect(exchange("/api/auth/login", {"password": PASSWORD})[0] == 503, "concurrent_hash_work_bounded")
        expect(poll(ticket)[0] == 401, "wrong_password_denied")
        status, headers, body = login(PASSWORD)
        expect(status == 200 and body.get("authenticated") is True, "correct_password_creates_session")
        set_cookie = headers.get("Set-Cookie", "")
        expect("HttpOnly" in set_cookie and "SameSite=Strict" in set_cookie and "Max-Age=28800" in set_cookie,
               "cookie_flags_and_lifetime")
        cookie = set_cookie.split(";", 1)[0]
        csrf = body["csrf"]
        auth_headers = {"Cookie": cookie, "X-CSRF-Token": csrf, "Origin": BASE}
        expect(exchange("/api/test/status", headers={"Cookie": cookie})[0] == 200, "authenticated_status_available")
        status, _, session, _ = exchange("/api/auth/session", headers={"Cookie": cookie})
        expect(status == 200 and session["csrf"] == csrf, "session_metadata_restores_csrf")
        expect(exchange("/api/test/status", headers={"Cookie": cookie + "; " + cookie})[0] == 401,
               "duplicate_cookie_denied")
        expect(exchange("/api/test/status", headers={"Cookie": "x" + cookie})[0] == 401,
               "cookie_name_prefix_does_not_match")
        expect(exchange("/api/test/mutate", {}, {"Cookie": cookie})[0] == 403, "mutation_without_csrf_denied")
        expect(exchange("/api/test/mutate", {}, {"Cookie": cookie, "X-CSRF-Token": "0" * 64})[0] == 403,
               "wrong_csrf_denied")
        expect(exchange("/api/test/mutate", {}, {**auth_headers, "Origin": "null"})[0] == 403,
               "mutation_wrong_origin_denied")
        expect(exchange("/api/test/mutate", {}, auth_headers)[0] == 200, "authorized_mutation_allowed")
        expect(exchange("/api/test/status", headers={"Cookie": cookie})[2]["mutations"] == 1,
               "denied_requests_do_not_mutate")
        # A verifier change must revoke both the session and a pending login.
        ticket = login(PASSWORD, completed=False)
        expect(exchange("/api/test/rotate", {}, auth_headers)[0] == 200, "verifier_change_accepted")
        expect(exchange("/api/test/status", headers={"Cookie": cookie})[0] == 401, "rotation_revokes_existing_session")
        expect(poll(ticket)[0] == 401, "rotation_revokes_inflight_login_result")
        status, headers, body = login(PASSWORD)
        expect(status == 200, "fresh_login_after_rotation")
        cookie, csrf = headers["Set-Cookie"].split(";", 1)[0], body["csrf"]
        expect(exchange("/api/auth/logout", {}, {"Cookie": cookie})[0] == 403, "logout_requires_csrf")
        status, headers, _, _ = exchange("/api/auth/logout", {}, {"Cookie": cookie, "X-CSRF-Token": csrf})
        expect(status == 200 and "Max-Age=0" in headers.get("Set-Cookie", ""), "logout_expires_cookie")
        expect(exchange("/api/test/status", headers={"Cookie": cookie})[0] == 401, "logout_revokes_access")
        expect(exchange("/api/auth/session", headers={"Cookie": cookie})[0] == 401, "logout_revokes_session_metadata")
        expect(responsiveness and max(responsiveness) < 2, "http_responsive_during_kdf")
        report.update(passed=True, check_count=len(checks), login_seconds=logins,
                      kdf_http_probes=len(responsiveness), max_http_seconds=round(max(responsiveness), 3))
    finally:
        if added:
            transport.netsh("connect", f"name={args.restore_profile}", f"interface={args.interface}", check=False)
            transport.netsh("delete", "profile", f"name={PROFILE}", f"interface={args.interface}", check=False)
        profile_path.unlink(missing_ok=True)
        (out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({k: v for k, v in report.items() if k != "checks"}), flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
