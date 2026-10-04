# Local WireGuard backend for the IDF 5 experiment

Source: [lumapu/WireGuard-ESP32-Arduino](https://github.com/lumapu/WireGuard-ESP32-Arduino),
commit `48f970462237eae678c29b62553afaf2e520bad4`, the tested snapshot of
[ciniml PR #52](https://github.com/ciniml/WireGuard-ESP32-Arduino/pull/52).
Only `src/`, `library.properties` and `LICENSE` were copied. Upstream BSD-3-Clause
and the crypto source licenses are retained. This is a local patched snapshot;
it is not an unmodified upstream release or an upstream endorsement.

## Local changes

- Normalize trailing whitespace/EOF in copied text files; license text and
  crypto operations are retained.
- Replace the remaining legacy Wi-Fi netif fallback with the pinned IDF 5
  ESP-NETIF APIs. Supply the actual Wi-Fi STA underlay explicitly.
- Resolve the endpoint before taking the lwIP TCP/IP core lock. Protect all
  interface/peer lifecycle and status operations with that lock. The tested
  SDK enables `CONFIG_LWIP_TCPIP_CORE_LOCKING` and thread-safety checks.
- Add a bounded single-peer `beginConfigured` interface with PSK, IPv4 allowed
  source network, keepalive and an explicit default-route option.
- Check initialization failures, clean up the peer, UDP socket, timer and
  interface, and restore the previous default interface on stop.
- Expose valid peer-session state, not a fabricated last-handshake timestamp.
  Status now applies the same age/counter and responder-confirmation limits
  as transport acceptance; retained previous-key flags alone are insufficient.
- Count authenticated plaintext data bytes after successful encrypted send
  and after authentication/replay/source checks on receive. Handshakes and
  keepalives do not contribute to these counters.
- Extend the known peer's allowed source CIDRs without resetting its keys,
  apply a bounded configurable MTU, and expose the owner-only netif handle
  for the separately locked product destination-route policy.
- Remove the lwIP netif before freeing its device context. Interface removal
  can synchronously output TCP resets for bound connections; retain valid state
  for those callbacks after clearing the peer. The partial-start cleanup uses
  the same ordering, under the existing TCP/IP core lock.

`beginConfigured(..., defaultRoute=false)` now passes the first integrated
camera gate. Allowed source addresses alone are not a destination route
implementation; the product adds its own policy. Listen port is fixed at
51820, one instance is supported, and WG-A retains DHCP DNS. Product external
preview, longer idle and full-tunnel acceptance remain separate gates. Twenty
integrated camera/audio lifecycle cycles and three Wi-Fi recoveries pass.

## Evidence and limits

The independent experiment built and ran on pioarduino 55.03.312-1,
Arduino 3.3.12, IDF 5.5.5. Twenty start/stop cycles, three controlled Wi-Fi
reconnections, authenticated bidirectional data, and LAN gateway HTTP passed.
A remote WireGuard device reached the experiment's read-only `/status` API.
See [backend report](../../docs/WIREGUARD_BACKEND.md) for the precise limits.

A later active-TCP teardown fixture reproduced a null-state panic in the former
shutdown ordering. The fixed fixture passes 32 hardware checks, including three
WG restarts with a real bound TCP PCB, new sessions and authenticated
bidirectional data, preserved network revision, and working camera/SD/JPEG.
The red run also activated the existing crash guard; normal authenticated OTA
restored healthy startup without changing that guard or RTC memory. The
explicit `xiao-appliance-wg-teardown-test` environment is not a release image:
it alone creates a test TCP connection to the known peer `10.8.0.1:9` immediately
before removal. Production builds compile neither that fixture nor connection.
See ignored `wg-teardown-red/` and `wg-teardown-fixed/` reports and serial logs.

The compiler emits an upstream X25519 array-size warning in the reference
implementation. Crypto primitives were not changed or independently audited.
The trial establishes compatibility and observed connectivity; it does not
establish final full-tunnel behavior, long-term stability or a security audit.
