"""Bounded upstream HTTP checks, with temporary AP Wi-Fi and profile restoration.

Uses no saved Wi-Fi passwords. This is a bring-up harness, not product code.
All media/reports stay under ignored .tools/hardware. Original LAN credentials
and profiles are unchanged. Run each stage separately to keep AP visits brief.
"""
import argparse
import array
import hashlib
import io
import json
import math
from pathlib import Path
import re
import subprocess
import time
import wave
from urllib.parse import urlencode
from urllib.request import Request, ProxyHandler, build_opener
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / ".tools/hardware/http"
PROFILE = "Codex-XIAO-hardware-test"
BASE = "http://192.168.4.1"
HTTP = build_opener(ProxyHandler({}))


def netsh(*args, check=True):
    result = subprocess.run(["netsh", "wlan", *args], capture_output=True, timeout=15)
    if check and result.returncode:
        raise RuntimeError("netsh operation failed")
    return result.stdout.decode("utf-8", "replace")


def request(path, data=None, timeout=8):
    req = Request(BASE + path, data=data)
    if data is not None:
        req.add_header("Content-Type", "application/octet-stream")
    with HTTP.open(req, timeout=timeout) as response:
        return response.read()


def control(key, value):
    return request("/control?" + urlencode({key: value}))


def wait_http(seconds=30, reconnect_interface=None):
    deadline = time.monotonic() + seconds
    reconnect_at = time.monotonic() + 5
    while time.monotonic() < deadline:
        try:
            return request("/", timeout=2)
        except OSError:
            if reconnect_interface and time.monotonic() >= reconnect_at:
                # Windows can abandon the AP profile while the radio restarts.
                # Retry within the same bounded visit using our temporary profile.
                netsh("connect", f"name={PROFILE}", f"interface={reconnect_interface}", check=False)
                reconnect_at = time.monotonic() + 5
            time.sleep(0.5)
    raise RuntimeError("Target AP HTTP did not become reachable")


def recordings():
    root = json.loads(control("sfile", "/"))
    folders = [path for path in root if re.fullmatch(r"/\d{8}", path)]
    if len(folders) > 8:
        raise RuntimeError("Too many recording folders for this bounded test")
    files = set()
    for folder in folders:
        files.update(path for path in json.loads(control("sfile", folder))
                     if path.lower().endswith(".avi"))
    return root, files


def play_test_tones():
    """Four quiet 880 Hz pulses through the PC's existing sound output."""
    import winsound
    samples = array.array("h", (int(2500 * math.sin(2 * math.pi * 880 * i / 16000))
                               if i % 16000 < 8000 else 0 for i in range(64000)))
    wav = io.BytesIO()
    with wave.open(wav, "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(16000)
        output.writeframes(samples.tobytes())
    winsound.PlaySound(wav.getvalue(), winsound.SND_MEMORY)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", choices=["assets", "probe", "record", "ota", "collect"])
    parser.add_argument("--ssid", required=True)
    parser.add_argument("--restore-profile", required=True)
    parser.add_argument("--interface", default="WLAN")
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--restart", action="store_true", help="Controlled upstream restart before testing")
    parser.add_argument("--record-seconds", type=int, default=20)
    parser.add_argument("--test-tones", action="store_true", help="Play four quiet PC tones during recording")
    collect_source = parser.add_mutually_exclusive_group()
    collect_source.add_argument("--previous-inventory", type=Path, help="For collect: require exactly one new AVI since this report")
    collect_source.add_argument("--sd-path", help="For collect: download one explicitly named existing AVI")
    args = parser.parse_args()
    if args.stage == "ota" and not args.firmware:
        parser.error("OTA requires --firmware")
    if not 5 <= args.record_seconds <= 180:
        parser.error("Record duration must be between 5 and 180 seconds")
    OUT.mkdir(parents=True, exist_ok=True)
    profile_path = OUT / "temporary-ap.xml"
    if PROFILE in netsh("show", "profiles"):
        raise SystemExit("Temporary profile already exists; inspect before reusing its name")
    profile_path.write_text(f'''<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
<name>{PROFILE}</name><SSIDConfig><SSID><name>{escape(args.ssid)}</name></SSID></SSIDConfig>
<connectionType>ESS</connectionType><connectionMode>manual</connectionMode>
<MSM><security><authEncryption><authentication>open</authentication><encryption>none</encryption><useOneX>false</useOneX></authEncryption></security></MSM>
</WLANProfile>''', encoding="utf-8")
    report = {"stage": args.stage, "ssid": args.ssid, "passed": False}
    added = False
    try:
        netsh("add", "profile", f"filename={profile_path}", "user=current", f"interface={args.interface}")
        added = True
        report["connect_result"] = netsh("connect", f"name={PROFILE}", f"interface={args.interface}")
        try:
            page = wait_http(45, reconnect_interface=args.interface)
        finally:
            (OUT / f"{args.stage}-wifi-state.txt").write_text(netsh("show", "interfaces"), encoding="utf-8")
            (OUT / f"{args.stage}-ip-state.txt").write_bytes(subprocess.run(["ipconfig"], capture_output=True, timeout=10).stdout)
        if args.restart:
            control("reset", "baseline-controlled-restart")
            time.sleep(3)
            # Windows may reconnect to the original network while the AP reboots.
            netsh("connect", f"name={PROFILE}", f"interface={args.interface}")
            page = wait_http(45, reconnect_interface=args.interface)
        (OUT / f"{args.stage}-page.html").write_bytes(page)
        report["http_root_bytes"] = len(page)
        if args.stage == "assets":
            for name in ("common.js", "MJPEG2SD.htm"):
                content = (ROOT / "data" / name).read_bytes()
                control("startOTA", name)
                response = request("/upload", content)
                if b"Completed upload file" not in response:
                    raise RuntimeError(f"Asset upload failed: {name}")
                report[name] = {"bytes": len(content), "sha256": hashlib.sha256(content).hexdigest()}
            control("reset", "baseline-assets-installed")
        elif args.stage == "probe":
            status = json.loads(request("/status"))
            report["status"] = {key: status.get(key) for key in
                                ("camModel", "card", "card_size", "free_bytes", "used_bytes", "framesize", "fps", "micGain", "xclkMhz", "forceRecord", "atemp")}
            still = control("still", "1")
            if not still.startswith(b"\xff\xd8") or not still.endswith(b"\xff\xd9"):
                raise RuntimeError("HTTP still is not a complete JPEG")
            (OUT / "still.jpg").write_bytes(still)
            report["still_bytes"] = len(still)
            with HTTP.open(BASE + "/sustain?stream=1", timeout=5) as response:
                data = b""
                deadline = time.monotonic() + 6
                while time.monotonic() < deadline and data.count(b"\xff\xd9") < 3:
                    data += response.read(4096)
                frames = re.findall(rb"\xff\xd8.*?\xff\xd9", data, re.S)
                for i, frame in enumerate(frames[:3]):
                    (OUT / f"stream-{i}.jpg").write_bytes(frame)
                report["stream_frames"] = len(frames[:3])
                if len(frames) < 3:
                    raise RuntimeError("Fewer than three preview frames received")
        elif args.stage == "record":
            control("clockUTC", int(time.time()))
            _, previous_files = recordings()
            control("micGain", "3")
            control("record", "1")
            control("forceRecord", "1")
            recording_started = time.monotonic()
            try:
                if args.test_tones:
                    time.sleep(5)
                    play_test_tones()
                    report["test_tones"] = {"frequency_hz": 880, "start_seconds": 5, "pulse_seconds": 0.5, "count": 4}
                time.sleep(max(0, args.record_seconds - (time.monotonic() - recording_started)))
            finally:
                control("forceRecord", "0")
            time.sleep(3)
            status = json.loads(request("/status"))
            report["status"] = {key: status.get(key) for key in
                                ("camModel", "fps", "micGain", "isCapturing", "atemp")}
            root, current_files = recordings()
            report["root_listing"] = root
            new_files = current_files - previous_files
            if len(new_files) != 1:
                raise RuntimeError(f"Expected one new AVI, found {len(new_files)}")
            path = new_files.pop()
            control("sfile", path)
            recording = request("/sustain?download=1", timeout=30)
            if not recording.startswith(b"RIFF") or recording[8:12] != b"AVI ":
                raise RuntimeError("Downloaded recording is not an AVI")
            (OUT / "baseline-recording.avi").write_bytes(recording)
            report["recording"] = {"sd_path": path, "bytes": len(recording),
                                   "requested_seconds": args.record_seconds,
                                   "sha256": hashlib.sha256(recording).hexdigest()}
        elif args.stage == "collect":
            _, files = recordings()
            report["files"] = sorted(files)
            if args.previous_inventory or args.sd_path:
                if args.sd_path:
                    path = args.sd_path
                    if path not in files or not re.fullmatch(r"/\d{8}/\d{8}_\d{6}_[A-Z0-9]+_\d+_\d+(?:_[SMC])*\.avi", path):
                        raise RuntimeError("Requested AVI is absent or not an upstream recording path")
                else:
                    previous = json.loads(args.previous_inventory.read_text(encoding="utf-8"))["files"]
                    new_files = files - set(previous)
                    if len(new_files) != 1:
                        raise RuntimeError(f"Expected one new AVI, found {len(new_files)}")
                    path = new_files.pop()
                control("sfile", path)
                recording = request("/sustain?download=1", timeout=30)
                if not recording.startswith(b"RIFF") or recording[8:12] != b"AVI ":
                    raise RuntimeError("Collected recording is not AVI")
                (OUT / "collected-recording.avi").write_bytes(recording)
                report["recording"] = {"sd_path": path, "bytes": len(recording),
                                       "sha256": hashlib.sha256(recording).hexdigest()}
        else:
            firmware = args.firmware.read_bytes()
            control("startOTA", "baseline-ota.bin")
            result = request("/upload", firmware, timeout=35)
            report["ota_response"] = result.decode("utf-8", "replace")
            report["firmware_sha256"] = hashlib.sha256(firmware).hexdigest()
            if b"OTA update complete" not in result:
                raise RuntimeError("OTA did not report completion")
            time.sleep(4)
            netsh("connect", f"name={PROFILE}", f"interface={args.interface}")
            restarted_page = wait_http(45, reconnect_interface=args.interface)
            report["reboot_http_root_bytes"] = len(restarted_page)
            status = json.loads(request("/status"))
            report["reboot_status"] = {key: status.get(key) for key in
                                       ("camModel", "card", "xclkMhz", "micGain")}
        report["passed"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        if added:
            netsh("connect", f"name={args.restore_profile}", f"interface={args.interface}")
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                state = netsh("show", "interfaces")
                if re.search(r"SSID\s*:\s*" + re.escape(args.restore_profile) + r"\s*$", state, re.M):
                    report["original_wifi_restored"] = True
                    break
                time.sleep(0.5)
            netsh("delete", "profile", f"name={PROFILE}", f"interface={args.interface}")
        profile_path.unlink(missing_ok=True)
        (OUT / f"{args.stage}-report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] and report.get("original_wifi_restored") else 1


if __name__ == "__main__":
    raise SystemExit(main())
