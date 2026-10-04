"""Validate a local single-peer IPv4 config and generate ignored spike credentials.

No key/password values are printed. The generated header is for isolated
hardware experiments, never for the final embedded product or Git.
"""
import argparse
import base64
import configparser
import ipaddress
import json
from pathlib import Path


def key(value):
    try:
        decoded = base64.b64decode(value, validate=True)
    except ValueError:
        raise ValueError("Invalid WireGuard key encoding") from None
    if len(decoded) != 32:
        raise ValueError("WireGuard keys must decode to 32 bytes")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path)
    parser.add_argument("--wifi-ssid", required=True)
    parser.add_argument("--password-file", type=Path, required=True)
    parser.add_argument("--test-host", default="")
    parser.add_argument("--test-port", type=int, default=80)
    parser.add_argument("--test-path", default="/")
    args = parser.parse_args()
    phase = "file/sections"
    try:
        config = configparser.ConfigParser(interpolation=None)
        config.read_string(args.config.read_text(encoding="utf-8-sig"))
        if set(config.sections()) != {"Interface", "Peer"}:
            raise ValueError("Exactly one Interface and one Peer are required")
        interface, peer = config["Interface"], config["Peer"]
        phase = "Interface.Address"
        address = ipaddress.IPv4Interface(interface["Address"].strip())
        phase = "Peer.AllowedIPs"
        networks = [ipaddress.ip_network(item.strip(), strict=False)
                    for item in peer["AllowedIPs"].split(",")]
        allowed = [item for item in networks if item.version == 4]
        if not allowed:
            raise ValueError("This planned IPv4-only spike requires IPv4 AllowedIPs")
        endpoint, separator, port = peer["Endpoint"].strip().rpartition(":")
        phase = "Peer.Endpoint"
        if not separator or not endpoint or ":" in endpoint or not 1 <= int(port) <= 65535:
            raise ValueError("Expected hostname/IPv4 Endpoint:port")
        password = args.password_file.read_text(encoding="utf-8-sig").rstrip("\r\n")
        phase = "Wi-Fi credentials"
        if not 1 <= len(args.wifi_ssid.encode()) <= 32 or not 8 <= len(password) <= 63:
            raise ValueError("Invalid Wi-Fi SSID/password length")
        if args.test_host:
            ipaddress.IPv4Address(args.test_host)
        phase = "keys/keepalive"
        values = {
            "WIFI_SSID": args.wifi_ssid, "WIFI_PASSWORD": password,
            "WG_ADDRESS": str(address.ip), "WG_NETMASK": str(address.netmask),
            "WG_PRIVATE_KEY": key(interface["PrivateKey"].strip()),
            "WG_PEER_PUBLIC_KEY": key(peer["PublicKey"].strip()),
            "WG_PRESHARED_KEY": key(peer["PresharedKey"].strip()) if peer.get("PresharedKey") else "",
            "WG_ENDPOINT": endpoint, "WG_ENDPOINT_PORT": int(port),
            "WG_KEEPALIVE": int(peer.get("PersistentKeepalive", "0")),
            "WG_ALLOWED_IPS": ",".join(map(str, allowed)),
            "WG_TEST_HOST": args.test_host, "WG_TEST_PORT": args.test_port,
            "WG_TEST_PATH": args.test_path,
        }
        if not 0 <= values["WG_KEEPALIVE"] <= 65535:
            raise ValueError("Invalid keepalive")
        target = Path(__file__).parent / "include/secrets.h"
        target.write_text("// Private isolated spike credentials; ignored by Git.\n#pragma once\n" +
                          "".join(f"#define {name} {json.dumps(value)}\n" for name, value in values.items()),
                          encoding="utf-8")
        print(json.dumps({"prepared": True, "output": str(target),
                          "peer_count": 1, "allowed_network_count": len(allowed),
                          "ipv6_networks_excluded": len(networks) - len(allowed),
                          "has_preshared_key": bool(values["WG_PRESHARED_KEY"]),
                          "full_tunnel_requested": any(item.prefixlen == 0 for item in allowed),
                          "http_probe_configured": bool(args.test_host)}))
        return 0
    except (OSError, ValueError, KeyError, configparser.Error):
        parser.exit(1, f"Local configuration rejected at {phase} (values withheld).\n")


if __name__ == "__main__":
    raise SystemExit(main())
