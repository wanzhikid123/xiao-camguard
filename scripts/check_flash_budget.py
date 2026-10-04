"""Validate an ESP32 OTA partition table and a built application binary."""
import argparse
import csv
import json
from pathlib import Path

FLASH_BYTES = 8 * 1024 * 1024
DEFAULT_LIMIT = 0.80


def number(text):
    text = text.strip()
    suffix = text[-1:].upper()
    if suffix in ("K", "M"):
        return int(text[:-1], 0) * {"K": 1024, "M": 1024 * 1024}[suffix]
    return int(text, 0)


def inspect_partitions(path):
    partitions = []
    for row in csv.reader(Path(path).read_text(encoding="utf-8").splitlines()):
        if not row or row[0].lstrip().startswith("#"):
            continue
        if len(row) < 5:
            raise ValueError("Partition row must have name, type, subtype, offset, size")
        name, kind, subtype = (field.strip() for field in row[:3])
        offset, size = number(row[3]), number(row[4])
        if not name or offset < 0x9000 or size <= 0:
            raise ValueError(f"Invalid partition: {name}")
        alignment = 0x10000 if kind == "app" else 0x1000
        if offset % alignment or size % 0x1000:
            raise ValueError(f"Partition alignment: {name}")
        if offset + size > FLASH_BYTES:
            raise ValueError(f"Partition exceeds 8 MB: {name}")
        partitions.append(dict(name=name, type=kind, subtype=subtype, offset=offset, size=size))
    partitions.sort(key=lambda item: item["offset"])
    if len({item["name"] for item in partitions}) != len(partitions):
        raise ValueError("Duplicate partition names")
    for left, right in zip(partitions, partitions[1:]):
        if left["offset"] + left["size"] > right["offset"]:
            raise ValueError(f"Partitions overlap: {left['name']}, {right['name']}")
    slots = [item for item in partitions if item["type"] == "app" and item["subtype"] in ("ota_0", "ota_1")]
    if len(slots) != 2 or {item["subtype"] for item in slots} != {"ota_0", "ota_1"}:
        raise ValueError("Both ota_0 and ota_1 are required")
    if not any(item["type"] == "data" and item["subtype"] == "nvs" for item in partitions):
        raise ValueError("NVS partition is required")
    if not any(item["type"] == "data" and item["subtype"] == "ota" and item["size"] >= 0x2000 for item in partitions):
        raise ValueError("OTA metadata partition is required")
    return partitions, min(item["size"] for item in slots)


def firmware_budget(binary, slot_size, limit=DEFAULT_LIMIT):
    if not 0 < limit <= 1:
        raise ValueError("Budget limit must be greater than 0 and at most 1")
    size = Path(binary).stat().st_size
    if not size:
        raise ValueError("Firmware binary is empty")
    result = dict(firmware_bytes=size, ota_slot_bytes=slot_size,
                  used_percent=round(size * 100 / slot_size, 2),
                  limit_percent=limit * 100, passed=size <= int(slot_size * limit))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--partitions", default="partitions/xiao_8mb_ota.csv")
    parser.add_argument("--bin", type=Path)
    parser.add_argument("--limit", type=float, default=DEFAULT_LIMIT)
    args = parser.parse_args()
    try:
        partitions, slot = inspect_partitions(args.partitions)
        report = dict(flash_bytes=FLASH_BYTES, ota_slot_bytes=slot, partitions=partitions)
        if args.bin:
            report.update(firmware_budget(args.bin, slot, args.limit))
        else:
            report["firmware_checked"] = False
        print(json.dumps(report, indent=2))
        return 1 if report.get("passed") is False else 0
    except (OSError, ValueError) as error:
        parser.exit(2, f"Budget check failed: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
