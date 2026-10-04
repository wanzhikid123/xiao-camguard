# WireGuard backend investigation

Updated 2026-10-03. The independent IDF 5 spike passed its bounded hardware
gate. The locally patched PR #52 snapshot is now integrated into the camera
application and passes its first 33-check WG-A product gate. User remote capture
and independent 30-minute idle now pass. The 20-cycle
product lifecycle and fault/UI gates now pass; see the product report below.

## Exact environment and candidates

pioarduino 55.03.312-1, Arduino 3.3.12, IDF 5.5.5 and XIAO ESP32S3 Sense.
The camera core was not downgraded.

| Candidate | Exact source pin | Actual result |
| --- | --- | --- |
| ciniml release 0.1.5 | a3307979bb5bfe4e767a9331f06cf1bc05136b1b | Compile failed: missing tcpip_adapter.h |
| ciniml main snapshot | 3048281829d14a3050faa19cfbe47146e33c0765 | Compile failed: missing tcpip_adapter.h |
| lumapu PR #52 | 48f970462237eae678c29b62553afaf2e520bad4 | Compile failed: legacy tcpip_adapter_get_netif / TCPIP_ADAPTER_IF_STA fallback |
| Local pr52-idf5 | Same PR snapshot plus source-controlled patches | Compile/upload passed; 20-cycle hardware gate passed |

Sources: [ciniml](https://github.com/ciniml/WireGuard-ESP32-Arduino),
[PR #52](https://github.com/ciniml/WireGuard-ESP32-Arduino/pull/52),
[lumapu](https://github.com/lumapu/WireGuard-ESP32-Arduino).
Local source and patch inventory: [vendor README](../vendor/wireguard-idf5/README.md).

## Observed hardware evidence

The owner's existing single-peer configuration and 2.4 GHz Wi-Fi were loaded
from ignored local files. Keys/passwords are excluded from reports and Git.
The experiment supports IPv4 only and explicitly reported excluding the
configuration's IPv6 default route.

- NTP synchronized before starting WireGuard.
- Twenty of twenty starts produced a valid authenticated peer session.
- All twenty HTTP probes returned 301, with nonzero authenticated plaintext
  transmit/receive counters. A 301 is a redirect, not a successful 2xx response.
- All forty LAN gateway HTTP probes passed: one while the tunnel was running
  and one after stopping it in each cycle.
- Three deliberate Wi-Fi disconnect/reconnect events recovered, at cycles
  5, 10 and 15, followed by successful session/data checks.
- The owner reported seeing the experiment's JSON through another WireGuard
  device at `http://10.8.0.5:8080/status`. A subsequent independent PC request
  also returned `experiment=true`, `peer_session_up=true` and data counters.
- The bounded trial completed after 588.3 seconds and stopped the tunnel.

Ignored evidence: `.tools/hardware/wg-twenty-cycles.log`,
`wg-cycle-report.json` and `wg-remote-status.json`.
The report is reproducible with `experiments/wireguard/analyze_cycles.py`.

## Memory and status interpretation

Initial free heap was 271,492 bytes. After-stop heap was 270,748 for cycles
1–7, varied slightly during later cycles, settled at 270,472 for cycles 14–19,
and was 270,424 at cycle 20. The final after-stop heap was 1,068 bytes below
the initial baseline. A ten-minute trial does not establish absence of leaks;
longer observations must distinguish retained network state from leaks.
Minimum heap was 267,652 bytes.

`initialized` describes an interface, not a handshake. `peer_session_up`
comes from valid peer-session keys in the backend. No last-handshake timestamp
is exposed or invented. Plaintext counters exclude handshakes and keepalives.
A successful remote JSON request supplies additional evidence for inbound
traffic through the tunnel.

## Remaining product gates

The spike temporarily switches the default interface to WireGuard and restores
the prior default on stop. It is not final WG-A or WG-B behavior.

The product now implements configuration parsing, an explicit destination
route policy preserving LAN/underlay access, lifecycle recovery, sanitized
errors, camera/recording coexistence and network-setting rollback. It extends
the adapter with up to eight allowed source CIDRs and actual netif MTU.
WG-A preserves DHCP DNS and excludes the imported default/IPv6 routes.
The actual destination-route echo test, 20 product lifecycle/audio cycles,
three Wi-Fi recoveries and fault/UI gates pass, as do user remote capture,
30-minute idle and whole 47.7 MB WG transfer. See [product evidence](WIREGUARD_REMOTE_ACCESS.md).

Before full tunnel is accepted, verify DNS policy, the public exit address,
endpoint/LAN exceptions, route restoration, VPN loss behavior and external
traffic evidence. IPv6 tunnel transport is unsupported; WG-B still needs an
external IPv6 no-leak policy. The observed public HTTP 301 and
counters alone do not satisfy that complete acceptance suite.

The compiler also emits an upstream reference X25519 array-size warning.
No crypto primitive was modified or independently audited.

## Active TCP teardown regression

An authenticated recovery restart exposed a null-state access during
`netif_remove`: lwIP aborts TCP PCBs using the removed local address and can
synchronously invoke the WG output callback. The former adapter had already
freed `netif->state`. The old-order hardware fixture reproduces this exact
`peer_lookup_by_allowed_ip` / TCP reset stack and `0x000000b0` fault. Its report
remains failed because the crash guard then prevented camera startup.

Keeping the device alive until interface removal finishes fixes this lifecycle
error. Timer/UDP cleanup and free follow removal within the same core lock;
peer keys are cleared first. The fixed explicit fixture passes **32 checks**:
three actual active TCP PCBs during teardown, no reset/panic in those cycles,
new WG sessions and bidirectional data, original network revision, usable JPEG,
SD and camera, and capture-value/240 MHz recovery. Normal authenticated OTA
recovers from the prior guard state without bypassing the guard. This fixture
is separate from normal firmware and must not remain deployed.

Evidence: ignored `wg-teardown-red/report.json`, `wg-teardown-fixed/report.json`
and their serial logs, with separately archived ELF/bin images.
