"""Real Wi-Fi rollback in the explicit network-trial fixture, not product NVS.

Credentials and a random test authorization token are supplied privately over
USB, never embedded or saved in reports. Verifies AP rejection, real LAN
confirmation, a 60 s failed-network rollback, reset recovery and reconnect.
"""
import argparse
import json
from pathlib import Path
import re
import secrets
import time
from urllib.error import HTTPError
from urllib.request import Request
from xml.sax.saxutils import escape
import serial
from esptool.reset import HardReset
import baseline_http as transport

ROOT = Path(__file__).resolve().parents[2]
PROFILE = "Codex-XIAO-network-trial"
SSID = "XIAO-Network-Test-084C"
AP = "http://192.168.4.1:8081"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--ssid", required=True, help="Known working 2.4 GHz Wi-Fi")
    parser.add_argument("--password-file", type=Path, default=ROOT / ".tools/private/wifi-password.txt")
    parser.add_argument("--restore-profile", required=True)
    parser.add_argument("--interface", default="WLAN")
    parser.add_argument("--out", type=Path, default=ROOT / ".tools/hardware/network-trial-report.json")
    args = parser.parse_args()
    token = secrets.token_hex(32)
    private_input = json.dumps({"ssid": args.ssid, "password": args.password_file.read_text(encoding="utf-8").rstrip("\r\n"),
                               "testToken": token}, ensure_ascii=False).encode("utf-8")
    if not 1 <= len(private_input) < 512:
        raise SystemExit("Private input exceeds fixture USB limit")
    if PROFILE in transport.netsh("show", "profiles"):
        raise SystemExit("Temporary test profile already exists; inspect before reusing it")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    profile_path = args.out.parent / "network-trial-ap.xml"
    profile_path.write_text(f'''<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
<name>{PROFILE}</name><SSIDConfig><SSID><name>{escape(SSID)}</name></SSID></SSIDConfig>
<connectionType>ESS</connectionType><connectionMode>manual</connectionMode>
<MSM><security><authEncryption><authentication>open</authentication><encryption>none</encryption><useOneX>false</useOneX></authEncryption></security></MSM>
</WLANProfile>''', encoding="utf-8")
    added = False
    report = dict(passed=False, checks=[], credentials_or_tokens_saved=False, product_nvs_applied=False)
    complete = False

    def expect(ok, name):
        report["checks"].append(dict(name=name, passed=bool(ok)))
        print(f"NETWORK HOST {'PASS' if ok else 'FAIL'} {name}", flush=True)
        if not ok:
            raise RuntimeError("Network trial check failed: " + name)

    def request(base, path, method="GET", authorized=True):
        headers = {"X-Network-Test": token} if authorized else {}
        try:
            response = transport.HTTP.open(Request(base + path, headers=headers, method=method), timeout=5)
        except HTTPError as error:
            response = error
        with response:
            return response.code, json.loads(response.read(2048))

    def restore():
        transport.netsh("connect", f"name={args.restore_profile}", f"interface={args.interface}", check=False)
        until = time.monotonic() + 18
        while time.monotonic() < until:
            if re.search(r"SSID\s*:\s*" + re.escape(args.restore_profile) + r"\s*$",
                         transport.netsh("show", "interfaces", check=False), re.M):
                return True
            time.sleep(.5)
        return False

    device = serial.Serial(port=None, baudrate=115200, timeout=.2, write_timeout=5)
    device.port = args.port; device.dtr = device.rts = False
    device.open()
    try:
        HardReset(device, uses_usb=True)()
        deadline = time.monotonic() + 200
        while time.monotonic() < deadline:
            line = device.readline().decode("utf-8", "replace").strip()
            if "Guru Meditation" in line or "panic'ed" in line or "Stack canary" in line or "assert failed:" in line:
                raise RuntimeError("Board fault during network fixture")
            if line == "NETWORK TRIAL READY":
                device.write(f"RUN_NETWORK_TRIAL {len(private_input)}\n".encode())
            elif line == "NETWORK TRIAL SEND":
                device.write(private_input)
                private_input = b""
            elif match := re.fullmatch(r"NETWORK TRIAL (PASS|FAIL) ([a-z_]+)", line):
                expect(match[1] == "PASS", "board_" + match[2])
            elif match := re.fullmatch(r"NETWORK TRIAL LAN phase=(first|recovered) address=(\d+\.\d+\.\d+\.\d+) revision=(\d+)", line):
                base = f"http://{match[2]}:8081"
                revision = int(match[3])
                if match[1] == "first":
                    transport.netsh("add", "profile", f"filename={profile_path}", "user=current", f"interface={args.interface}")
                    added = True
                    transport.netsh("connect", f"name={PROFILE}", f"interface={args.interface}")
                    transport.PROFILE = PROFILE; transport.BASE = AP
                    transport.wait_http(25, reconnect_interface=args.interface)
                    expect(request(AP, "/network-test/status", authorized=False)[0] == 401, "fixture_status_requires_private_token")
                    code, state = request(AP, "/network-test/status")
                    expect(code == 200 and state["wifiConnected"] and state["rescueAP"], "pending_wifi_and_recovery_ap_coexist")
                    expect(request(AP, f"/network-test/confirm?revision={revision}", "POST")[0] == 403,
                           "ap_socket_cannot_confirm_candidate_lan")
                    expect(restore(), "original_wifi_restored_before_lan_confirmation")
                    expect(request(base, f"/network-test/confirm?revision={revision + 1}", "POST")[0] == 409,
                           "wrong_revision_cannot_activate")
                    expect(request(base, f"/network-test/confirm?revision={revision}", "POST")[0] == 200,
                           "actual_lan_http_confirms_working_candidate")
                else:
                    code, state = request(base, "/network-test/status")
                    expect(code == 200 and state["state"] == "ready" and state["wifiConnected"] and not state["rescueAP"],
                           "real_lan_http_survives_failure_reset_and_reconnect")
            elif match := re.fullmatch(r"NETWORK TRIAL COMPLETE checks=(\d+) failures=0 product_nvs_untouched=1", line):
                complete = True
                report.update(passed=True, board_check_count=int(match[1]), check_count=len(report["checks"]))
                break
        if not complete:
            raise RuntimeError("Network fixture did not complete within 200 seconds")
    except Exception as error:
        report["error"] = type(error).__name__ + ": " + str(error)
    finally:
        if not complete:
            try:
                device.write(b"ABORT_NETWORK_TEST\n")
                until = time.monotonic() + 8
                while time.monotonic() < until:
                    if device.readline().strip() == b"NETWORK TRIAL ABORTED cleanup=1":
                        report["aborted_fixture_cleanup"] = True
                        break
            except (OSError, serial.SerialException):
                report["aborted_fixture_cleanup"] = False
        device.close()
        report["original_wifi_restored"] = restore()
        if added:
            transport.netsh("delete", "profile", f"name={PROFILE}", f"interface={args.interface}", check=False)
        profile_path.unlink(missing_ok=True)
        if not report["original_wifi_restored"]:
            report["passed"] = False
        private_input = b""; token = ""
        args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({key: value for key, value in report.items() if key != "checks"}), flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
