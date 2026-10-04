"""Summarize a bounded serial spike without retaining network credentials."""
import argparse
import json
import re
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    lines = args.log.read_text(encoding="utf-8", errors="replace").splitlines()
    cycles = []
    current = None
    reconnects = 0
    completed = False
    for line in lines:
        memory = re.fullmatch(r"(before_start|after_start|after_stop) cycle=(\d+) heap=(\d+) min_heap=(\d+) psram_free=(\d+) baseline_heap=(\d+)", line)
        if memory:
            phase, cycle, heap, minimum, psram, baseline = memory.groups()
            if phase == "before_start":
                current = {"cycle": int(cycle), "lan_checks": []}
                cycles.append(current)
            if current and current["cycle"] == int(cycle):
                current[phase] = {"heap": int(heap), "min_heap": int(minimum),
                                  "psram_free": int(psram), "baseline_heap": int(baseline)}
        elif current and line.startswith("WireGuard initialized="):
            current["initialized"] = line.startswith("WireGuard initialized=true;")
        elif current and line.startswith("Authenticated peer session up="):
            current["peer_session_up"] = line.startswith("Authenticated peer session up=true;")
        elif current and (match := re.fullmatch(r"Tunnel HTTP probe status=(\d+); 2xx=(true|false)", line)):
            current["http_status"] = int(match[1])
        elif current and (match := re.fullmatch(r"Authenticated tunnel plaintext bytes: tx=(\d+) rx=(\d+)", line)):
            current["tx_bytes"], current["rx_bytes"] = int(match[1]), int(match[2])
        elif current and line.startswith("LAN gateway HTTP reachable="):
            current["lan_checks"].append(line.endswith("true"))
        elif line == "Controlled WiFi reconnect test":
            reconnects += 1
        elif line == "WG SPIKE COMPLETE; tunnel stopped and LAN default restored":
            completed = True
    passed = (completed and [item["cycle"] for item in cycles] == list(range(1, 21))
              and reconnects == 3 and all(
                  item.get("initialized") and item.get("peer_session_up")
                  and item.get("tx_bytes", 0) > 0 and item.get("rx_bytes", 0) > 0
                  and 100 <= item.get("http_status", 0) <= 599
                  and item["lan_checks"] == [True, True]
                  and all(phase in item for phase in ("before_start", "after_start", "after_stop"))
                  for item in cycles))
    report = {"experiment": True, "cycle_gate_pass": passed, "completed": completed,
              "cycles": len(cycles), "controlled_wifi_reconnections": reconnects,
              "valid_peer_sessions": sum(bool(item.get("peer_session_up")) for item in cycles),
              "lan_checks_passed": sum(sum(item["lan_checks"]) for item in cycles),
              "http_statuses": sorted({item.get("http_status", 0) for item in cycles}),
              "full_tunnel_acceptance": "pending", "long_term_memory_leak_check": "pending",
              "observations": cycles}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key != "observations"}))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
