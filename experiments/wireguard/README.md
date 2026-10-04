# Independent WireGuard compatibility spike

The patched `pr52-idf5` environment built and passed a real bounded 20-cycle
test on XIAO ESP32S3 Sense. The unmodified release, main and PR #52 environments
all failed compilation on the pinned Arduino 3.3.12 / IDF 5.5.5 platform.
This experiment is excluded from the root camera firmware source filter.

See [backend results](../../docs/WIREGUARD_BACKEND.md) and
[patch provenance](../../vendor/wireguard-idf5/README.md).

From the repository root:

```powershell
python scripts/pio.py run -d experiments/wireguard -e released
python scripts/pio.py run -d experiments/wireguard -e main-snapshot
python scripts/pio.py run -d experiments/wireguard -e pr52
python scripts/pio.py run -d experiments/wireguard -e pr52-idf5
```

The first three commands reproduce the observed compatibility failures.
The final environment uses the source-controlled local backend.

## Local configuration

Copy `include/secrets.h.example` to ignored `include/secrets.h` or use
`prepare_secrets.py` with your local single-peer .conf and password file.
Never commit credentials. Without the private header, empty example
credentials make the experiment idle without connecting.

```powershell
python experiments/wireguard/prepare_secrets.py YOUR_LOCAL_CONFIG.conf --wifi-ssid YOUR_SSID --password-file .tools/private/wifi-password.txt --test-host YOUR_TUNNEL_SERVICE_IPV4
python scripts/pio.py run -d experiments/wireguard -e pr52-idf5 -t upload --upload-port COM3
python scripts/serial_capture.py --port COM3 --seconds 850 --out .tools/hardware/wg-twenty-cycles.log --reset --stop-when 'WG SPIKE COMPLETE'
python experiments/wireguard/analyze_cycles.py .tools/hardware/wg-twenty-cycles.log --out .tools/hardware/wg-cycle-report.json
```

The bounded patched trial currently requires one IPv4 AllowedIPs entry
`0.0.0.0/0`. An IPv6 entry in the input is explicitly excluded and counted.
PSK and keepalive are supported. Endpoint hostname resolution precedes the
lwIP core lock. Do not use this experiment as the final camera network policy.

The read-only diagnostic API is `http://WG_ADDRESS:8080/status`, and reports
only experimental status/session/counters. It has no configuration endpoints.
Use an IPv4 HTTP probe service; response content is never printed. The test
switches the default interface, checks LAN gateway HTTP before/after stopping,
and deliberately reconnects Wi-Fi after cycles 5, 10 and 15.

Twenty starts, valid session keys, bidirectional data, all forty LAN checks and
three Wi-Fi reconnections passed. A remote WG device and a PC VPN connection
also fetched the diagnostic JSON. The observed HTTP service returned a 301
redirect. Full-tunnel acceptance, long-term memory behavior and camera
coexistence remain pending.
