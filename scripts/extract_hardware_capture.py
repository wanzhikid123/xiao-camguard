"""Verify and extract the isolated diagnostic firmware's USB file frames."""
import argparse
import hashlib
import json
from pathlib import Path
import re

FRAME = re.compile(rb"\nHWFILE (camera\.jpg|microphone\.wav) (\d+) ([0-9a-f]{32})\r?\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()
    raw = args.capture.read_bytes()
    cursor = 0
    transcript = []
    files = []
    args.out_dir.mkdir(parents=True, exist_ok=True)
    while match := FRAME.search(raw, cursor):
        transcript.append(raw[cursor:match.start()])
        name = match[1].decode("ascii")
        size = int(match[2])
        if not 0 < size <= 16 * 1024 * 1024:
            raise SystemExit("Invalid hardware frame size")
        end = match.end() + size
        payload = raw[match.end():end]
        expected_end = re.compile(rb"\r?\nHWFILE_END " + match[1] + rb" " + str(size).encode() + rb"\r?\n")
        trailer = expected_end.match(raw, end)
        checksum = hashlib.md5(payload).hexdigest()
        if len(payload) != size or checksum != match[3].decode("ascii") or trailer is None:
            raise SystemExit(f"Corrupt or incomplete USB transfer: {name}")
        (args.out_dir / name).write_bytes(payload)
        files.append(dict(name=name, bytes=size, md5=checksum, verified=True))
        cursor = trailer.end()
    transcript.append(raw[cursor:])
    text = b"".join(transcript).decode("utf-8", "replace")
    (args.out_dir / "diagnostics.log").write_text(text, encoding="utf-8")
    report = dict(files=files, completed="HW DONE" in text)
    (args.out_dir / "transfer-report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["completed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
