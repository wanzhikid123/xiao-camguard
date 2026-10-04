"""Capture bounded hardware boot logs using the installed esptool environment."""
import argparse
from pathlib import Path
import time

import serial
from esptool.reset import HardReset


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--reset", action="store_true")
    parser.add_argument("--send-when", help="Send a command only after this USB log marker")
    parser.add_argument("--command", help="USB command, sent once with a newline")
    parser.add_argument("--stop-when", help="End capture after this completion marker")
    args = parser.parse_args()
    if bool(args.send_when) != bool(args.command):
        parser.error("--send-when and --command must be used together")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    device = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    device.port = args.port
    device.dtr = False
    device.rts = False
    device.open()
    with device, args.out.open("wb") as log:
        if args.reset:
            HardReset(device, uses_usb=True)()
        started = time.monotonic()
        deadline = started + args.seconds
        count = 0
        recent = b""
        sent = False
        while time.monotonic() < deadline:
            data = device.read(device.in_waiting or 1)
            if data:
                log.write(data)
                log.flush()
                count += len(data)
                recent = (recent + data)[-4096:]
                if args.send_when and not sent:
                    if args.send_when.encode("utf-8") in recent:
                        device.write(args.command.encode("utf-8") + b"\n")
                        sent = True
                        print("Sent the requested USB command after its ready marker", flush=True)
                if args.stop_when and args.stop_when.encode("utf-8") in recent:
                    break
    print(f"Captured {count} bytes in {time.monotonic() - started:.1f}s to {args.out}")


if __name__ == "__main__":
    main()
