"""Real product enrollment and Wi-Fi rollback; reads private files locally.

Creates the user-selected admin account in product camcfg by the first-run HTTP
flow, never embeds/prints credentials or saves cookies/tickets in reports.
Records during a failed Wi-Fi trial, reboots during another trial, then leaves
the appliance using its confirmed Wi-Fi. Restores the original PC profile.
"""
import argparse
import json
from pathlib import Path
import re
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request
from xml.sax.saxutils import escape
import baseline_http as transport

ROOT = Path(__file__).resolve().parents[2]
PROFILE = "Codex-XIAO-product-setup"
SSID = "XIAO-Camera-084C"
AP = "http://192.168.4.1"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ssid", required=True)
    parser.add_argument("--restore-profile", required=True)
    parser.add_argument("--interface", default="WLAN")
    parser.add_argument("--password-file", type=Path, default=ROOT / ".tools/private/admin-password.txt")
    parser.add_argument("--wifi-password-file", type=Path, default=ROOT / ".tools/private/wifi-password.txt")
    parser.add_argument("--out", type=Path, default=ROOT / ".tools/hardware/appliance-setup-report.json")
    parser.add_argument("--resume-lan", help="Resume validation of an already-enrolled product at this IPv4 address")
    args = parser.parse_args()
    password = args.password_file.read_text(encoding="utf-8").rstrip("\r\n")
    wifi_password = args.wifi_password_file.read_text(encoding="utf-8").rstrip("\r\n")
    if PROFILE in transport.netsh("show", "profiles"):
        raise SystemExit("Temporary product setup profile exists; inspect before reuse")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    xml = args.out.parent / "product-setup-ap.xml"
    xml.write_text(f'''<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
<name>{PROFILE}</name><SSIDConfig><SSID><name>{escape(SSID)}</name></SSID></SSIDConfig>
<connectionType>ESS</connectionType><connectionMode>manual</connectionMode>
<MSM><security><authEncryption><authentication>open</authentication><encryption>none</encryption><useOneX>false</useOneX></authEncryption></security></MSM>
</WLANProfile>''', encoding="utf-8")
    report = dict(passed=False, checks=[], credentials_or_tokens_saved=False, product_enrolled=False)
    added = False
    cookie = csrf = ticket = ""
    base = AP
    latencies = []

    def expect(ok, name):
        report["checks"].append(dict(name=name, passed=bool(ok)))
        print(f"PRODUCT {'PASS' if ok else 'FAIL'} {name}", flush=True)
        if not ok:
            raise RuntimeError("Product setup check failed: " + name)

    def request(path, method="GET", value=None, headers=None, raw=None):
        data = json.dumps(value, ensure_ascii=False).encode() if value is not None else raw
        supplied = dict(headers or {})
        if data is not None:
            supplied["Content-Type"] = "application/json"
        req = Request(base + path, data=data, headers=supplied, method=method)
        start = time.monotonic()
        for attempt in range(4):
            try:
                response = transport.HTTP.open(req, timeout=5)
            except HTTPError as error:
                response = error
            except URLError as error:
                if method == "GET" and getattr(error.reason, "winerror", 0) in (10051, 10065) and attempt < 3:
                    time.sleep(.3)
                    continue
                raise
            break
        with response:
            payload = response.read(250000)
            if password.encode() in payload or wifi_password.encode() in payload:
                raise RuntimeError("Private input appeared in product response")
            latencies.append(time.monotonic() - start)
            return response.code, dict(response.headers), payload

    def state(path="/api/status", setup=False):
        headers = {"X-Setup-Ticket": ticket, "X-Appliance-Request": "1"} if setup else {"Cookie": cookie}
        code, _, body = request(path, headers=headers)
        if code != 200:
            raise RuntimeError("Product state endpoint refused request: " + str(code))
        return json.loads(body)

    def mutate(path, value=None):
        return request(path, "POST", value, {"Cookie": cookie, "X-CSRF-Token": csrf, "Origin": base})

    def wait_http(seconds=30, reconnect=False):
        deadline = time.monotonic() + seconds
        reconnect_at = time.monotonic() + 4
        while time.monotonic() < deadline:
            try:
                if request("/api/bootstrap")[0] == 200:
                    request("/")  # Normal reachable-page boot health check.
                    return
            except OSError:
                pass
            if reconnect and time.monotonic() >= reconnect_at:
                transport.netsh("connect", f"name={PROFILE}", f"interface={args.interface}", check=False)
                reconnect_at = time.monotonic() + 4
            time.sleep(.5)
        raise RuntimeError("Product HTTP did not become reachable")

    def restore():
        transport.netsh("connect", f"name={args.restore_profile}", f"interface={args.interface}", check=False)
        until = time.monotonic() + 20
        while time.monotonic() < until:
            if re.search(r"SSID\s*:\s*" + re.escape(args.restore_profile) + r"\s*$",
                         transport.netsh("show", "interfaces", check=False), re.M):
                return True
            time.sleep(.5)
        return False

    def visit_ap():
        nonlocal added, base
        if not added:
            transport.netsh("add", "profile", f"filename={xml}", "user=current", f"interface={args.interface}")
            added = True
        transport.netsh("connect", f"name={PROFILE}", f"interface={args.interface}")
        base = AP
        wait_http(25, reconnect=True)

    def login():
        nonlocal cookie, csrf
        start = time.monotonic()
        code, _, body = request("/api/auth/login", "POST", {"password": password},
                                {"X-Appliance-Request": "1", "Origin": base})
        expect(code == 202, "real_password_login_accepted")
        login_ticket = json.loads(body)["ticket"]
        until = time.monotonic() + 120
        while time.monotonic() < until:
            code, headers, body = request("/api/auth/result", headers={"X-Login-Ticket": login_ticket})
            if code == 202:
                time.sleep(.7)
                continue
            expect(code == 200, "real_password_login_verified")
            cookie = headers["Set-Cookie"].split(";", 1)[0]
            csrf = json.loads(body)["csrf"]
            report.setdefault("login_seconds", []).append(round(time.monotonic() - start, 3))
            return
        raise RuntimeError("Real password login timed out")

    try:
        if args.resume_lan:
            if not re.fullmatch(r"\d+\.\d+\.\d+\.\d+", args.resume_lan):
                raise RuntimeError("Resume address must be IPv4")
            expect(restore(), "original_wifi_before_resume")
            base = "http://" + args.resume_lan
            wait_http()
            expect(json.loads(request("/api/bootstrap")[2])["adminConfigured"], "existing_product_account")
            report["product_enrolled"] = True
        else:
            visit_ap()
            bootstrap = json.loads(request("/api/bootstrap")[2])
            expect(bootstrap.get("adminConfigured") is False and len(bootstrap.get("setupNonce", "")) == 64,
                   "empty_product_has_ap_setup_nonce")
            nonce_headers = {"X-Setup-Nonce": bootstrap["setupNonce"], "X-Appliance-Request": "1", "Origin": AP}
            for path in ("/api/status", "/api/network", "/api/camera/still", "/api/camera/stream"):
                expect(request(path)[0] == 401, "unconfigured_locked_" + path)
            real = dict(ssid=args.ssid, wifiPassword=wifi_password, adminPassword=password)
            expect(request("/api/setup", "POST", real)[0] == 403, "setup_requires_nonce_and_marker")
            expect(request("/api/setup", "POST", real, {**nonce_headers, "Origin": "http://unrelated.invalid"})[0] == 403,
                   "setup_rejects_cross_origin")
            for name, change in (("short_password", {"adminPassword": "too-short"}),
                                 ("oversize_ssid", {"ssid": "x" * 33}),
                                 ("unknown_field", {"unexpected": "value"}),
                                 ("nested_json", {"adminPassword": {"value": "unexpected"}})):
                expect(request("/api/setup", "POST", {**real, **change}, nonce_headers)[0] == 400, "setup_rejects_" + name)
            expect(request("/api/setup", "POST", headers=nonce_headers,
                           raw=b'{"ssid":"test\\u0000tail","wifiPassword":"","adminPassword":"test-password"}')[0] == 400,
                   "setup_rejects_decoded_nul")
            start = time.monotonic()
            code, _, body = request("/api/setup", "POST", real, nonce_headers)
            real.clear()
            expect(code == 202, "chosen_password_setup_accepted")
            ticket = json.loads(body)["ticket"]
            expect(request("/api/setup/result")[0] == 403, "setup_result_requires_private_ticket")
            expect(request("/api/setup", "POST", dict(ssid=args.ssid, wifiPassword=wifi_password, adminPassword=password),
                           nonce_headers)[0] == 409, "overlapping_enrollment_rejected")
            until = time.monotonic() + 120
            while time.monotonic() < until:
                pending = state("/api/setup/result", setup=True)
                if pending["phase"] == "awaiting_local_confirmation":
                    break
                if pending["phase"] == "failed":
                    raise RuntimeError("Enrollment trial failed: " + pending["error"])
                expect(request("/api/status")[0] == 401, "camera_locked_until_active") if len(report["checks"]) < 20 else None
                time.sleep(1)
            else:
                raise RuntimeError("Enrollment did not reach LAN confirmation")
            report["enrollment_seconds"] = round(time.monotonic() - start, 3)
            revision = pending["revision"]
            confirm_headers = {"X-Setup-Ticket": ticket, "X-Appliance-Request": "1"}
            expect(request("/api/setup/confirm", "POST", {"revision": revision}, confirm_headers)[0] == 403,
                   "ap_cannot_promote_product_config")
            expect(restore(), "original_wifi_before_product_confirmation")
            base = "http://" + pending["lanIP"]
            report["lan_address"] = pending["lanIP"]
            wait_http(12)
            expect(request("/api/setup/confirm", "POST", {"revision": revision + 1}, confirm_headers)[0] == 409,
                   "stale_lan_revision_rejected")
            expect(request("/api/setup/confirm", "POST", {"revision": revision}, confirm_headers)[0] == 202,
                   "real_lan_confirmation_accepted")
            until = time.monotonic() + 10
            while time.monotonic() < until and state("/api/setup/result", setup=True)["phase"] != "active":
                time.sleep(.2)
            expect(json.loads(request("/api/bootstrap")[2]) == {"adminConfigured": True}, "account_activated_no_public_nonce")
            report["product_enrolled"] = True
            expect(request("/api/setup", "POST", {})[0] == 409, "active_account_cannot_be_overwritten_by_setup")
        lan = base
        report["lan_address"] = base.removeprefix("http://")
        login()
        initial = state()
        if not initial["cameraReady"]:
            expect(mutate("/api/device/restart")[0] == 202, "normal_recovery_restart")
            time.sleep(5); wait_http(); login(); initial = state()
        expect(initial["cameraReady"] and initial["sdReady"] and initial["audioEnabled"] and initial["fps"] == 15,
               "configured_camera_sd_audio_vga15_ready")
        selected_revision = initial["configRevision"]
        report["active_revision"] = selected_revision
        if initial["recordRequested"] or initial["recording"]:
            expect(mutate("/api/record/stop")[0] == 202, "finish_recording_left_by_interrupted_host_test")
            until = time.monotonic() + 25
            while time.monotonic() < until and state()["recording"]:
                time.sleep(.5)
            expect(not state()["recording"], "previous_recording_finalized_before_resume")
        code, _, jpeg = request("/api/camera/still", headers={"Cookie": cookie})
        expect(code == 200 and jpeg[:2] == b'\xff\xd8' and jpeg[-2:] == b'\xff\xd9', "configured_still_jpeg")
        (args.out.parent / "product-setup-still.jpg").write_bytes(jpeg)
        expect(request("/api/network/trial", "POST", {"ssid": "test-network", "wifiPassword": ""}, {"Cookie": cookie})[0] == 403,
               "network_edits_require_csrf")
        expect(mutate("/api/record/start")[0] == 202, "start_recording_before_failed_network_trial")
        time.sleep(2)
        expect(state()["recording"], "recording_active_before_wifi_change")
        started_trial = time.monotonic()
        expect(mutate("/api/network/trial", {"ssid": "codex-missing-wifi-084c", "wifiPassword": "synthetic-wifi-password"})[0] == 202,
               "bad_wifi_staged_in_product")
        time.sleep(2); visit_ap()
        ap_state = state()
        expect(ap_state["cameraReady"] and ap_state["recording"], "camera_and_recording_continue_on_recovery_ap")
        expect(request("/api/status")[0] == 401, "recovery_ap_status_remains_authenticated")
        expect(mutate("/api/network/trial", {"ssid": args.ssid, "wifiPassword": wifi_password})[0] == 409,
               "overlapping_product_network_trial_rejected")
        # Wait for the owner's known 60 s rollback window. Keep requests bounded,
        # then switch the PC back before the recovery AP closes on reconnection.
        while time.monotonic() - started_trial < 61:
            expect(state()["recording"], "recording_survives_wifi_trial") if time.monotonic() - started_trial > 50 else None
            time.sleep(2)
        expect(restore(), "original_wifi_after_product_rollback")
        base = lan; wait_http(25)
        recovered = state()
        expect(recovered["cameraReady"] and recovered["recording"] and recovered["configRevision"] == selected_revision,
               "bad_wifi_restores_active_config_without_stopping_recording")
        net = state("/api/network")
        until = time.monotonic() + 12
        while time.monotonic() < until and net["state"] != "ready":
            time.sleep(.3)
            net = state("/api/network")
        expect(net["state"] == "ready" and not net["rescueAP"] and not net["revision"], "product_rollback_closes_rescue_ap")
        expect(mutate("/api/record/stop")[0] == 202, "stop_network_trial_recording")
        until = time.monotonic() + 20
        while time.monotonic() < until and state()["recording"]:
            time.sleep(.5)
        expect(not state()["recording"], "network_trial_recording_finalized")
        expect(mutate("/api/network/trial", {"ssid": "codex-missing-wifi-084c", "wifiPassword": "synthetic-wifi-password"})[0] == 202,
               "second_bad_wifi_trial_started")
        time.sleep(2); visit_ap()
        expect(mutate("/api/device/restart")[0] == 202, "reboot_during_product_pending_trial")
        time.sleep(3); expect(restore(), "original_wifi_after_product_reset")
        base = lan; wait_http(30)
        expect(request("/api/status", headers={"Cookie": cookie})[0] == 401, "reboot_revokes_old_session")
        login()
        final = state()
        expect(final["cameraReady"] and final["sdReady"] and final["configRevision"] == selected_revision,
               "reboot_keeps_password_and_last_known_good_config")
        net = state("/api/network")
        until = time.monotonic() + 12
        while time.monotonic() < until and net["state"] != "ready":
            time.sleep(.3)
            net = state("/api/network")
        expect(not net["revision"] and net["state"] == "ready", "interrupted_candidate_not_retried")
        expect(mutate("/api/auth/logout")[0] == 200, "final_product_logout")
        cookie = csrf = ticket = ""
        report.update(passed=True, check_count=len(report["checks"]), maximum_http_seconds=round(max(latencies), 3))
    except Exception as error:
        # No raw exceptions, bodies, locals, or request objects in evidence.
        report.update(error_type=type(error).__name__, check_count=len(report["checks"]))
        if isinstance(error, RuntimeError):
            report["error_code"] = str(error)  # Only fixed harness messages/enums.
        report["pc_network_at_failure"] = transport.netsh("show", "interfaces", check=False)
        report["recovery_ap_visible_at_failure"] = SSID in transport.netsh("show", "networks", "mode=bssid", f"interface={args.interface}", check=False)
        print("PRODUCT stopped: " + type(error).__name__, flush=True)
    finally:
        if cookie:
            try:
                mutate("/api/record/stop")
                mutate("/api/auth/logout")
            except OSError:
                pass
        cookie = csrf = ticket = password = wifi_password = ""
        report["original_wifi_restored"] = restore()
        if added:
            transport.netsh("delete", "profile", f"name={PROFILE}", f"interface={args.interface}", check=False)
        xml.unlink(missing_ok=True)
        args.out.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: val for key, val in report.items() if key not in ("checks", "pc_network_at_failure")}, indent=2))
    return 0 if report["passed"] and report["original_wifi_restored"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
