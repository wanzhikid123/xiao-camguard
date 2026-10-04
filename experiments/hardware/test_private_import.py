"""Validate a local WG file over USB without storing keys on the test device.

Requires the isolated auth-http fixture. Its only response is a sanitized import
summary; no network/NVS application. Input bytes and raw serial lines are never
printed or written to a report. The real config remains in its ignored file.
"""
import argparse
import json
from pathlib import Path
import re
import time
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path)
    parser.add_argument("--port", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    text = args.config.read_bytes()
    if not 1 <= len(text) <= 4096:
        raise SystemExit("Configuration must be 1..4096 bytes")
    text.decode("utf-8")
    device = serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=5)
    device.port = args.port
    device.dtr = device.rts = False
    device.open()
    pattern = re.compile(rb"PRIVATE IMPORT (PASS|FAIL) error=([a-z0-9_]+) line=(\d+) ipv4=(\d+) excluded_ipv6=(\d+) excluded_addresses=(\d+) full_requested=(\d+) full_enabled=(\d+)")
    with device:
        device.write(f"IMPORT_WG {len(text)}\n".encode())
        deadline = time.monotonic() + 12
        sent = False
        while time.monotonic() < deadline:
            line = device.readline().strip()
            if line == b"PRIVATE IMPORT SEND":
                device.write(text)
                sent = True
            if match := pattern.fullmatch(line):
                state, error, *numbers = match.groups()
                values = list(map(int, numbers))
                report = dict(passed=sent and state == b"PASS", error=error.decode("ascii"),
                              error_line=values[0], ipv4_networks=values[1], excluded_ipv6_networks=values[2],
                              excluded_ipv6_addresses=values[3], full_tunnel_requested=bool(values[4]),
                              full_tunnel_enabled=bool(values[5]), nvs_or_network_applied=False,
                              credentials_logged=False)
                args.out.parent.mkdir(parents=True, exist_ok=True)
                args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
                print(json.dumps(report))
                return 0 if report["passed"] else 1
    raise SystemExit("No sanitized import response within the bounded USB test")


if __name__ == "__main__":
    raise SystemExit(main())
