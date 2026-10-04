# WG-B full-tunnel routing

Updated 2026-10-04. The production candidate now implements an explicit mode
trial, persistent IPv4 full routing, physical-output protection, bounded
bootstrap and a real public-exit verification job. The physical-policy fixture
passes 50 checks, including 75 output-packet cases and actual fault recovery.
The normal backend passes 35 mode/lifecycle checks and four admitted public
probes with zero retained heap loss. The final normal image additionally passes
42 actual Chrome/media checks. Four automatic refreshes also pass a separate
190-second observation; fresh remote-peer confirmation remains pending. The
historical RAM spike below is a
separate gate.

## Verified starting point

The previous WG-A-only driver refused `wg.fullTunnel`; text import retains IPv4
AllowedIPs but explicitly leaves that mode off. `WireGuardRoutes.cpp` wraps
the SDK's strong `ip4_route_src_hook`, preserves STA endpoint/LAN/AP traffic and
installs only non-default destination CIDRs. Existing peer-session keys alone
are not an outbound-route or server-NAT proof.

## Production policy

`POST /api/wireguard/mode` accepts only `remote_access` or `full_tunnel` and
requires the current session, CSRF, same Origin and the actual LAN socket.
It changes only the selected WG mode using the existing pending/tried bank,
local confirmation and automatic rollback. Import starts in remote-access
mode so keys and reachability can be checked before enabling the default route.
Full mode requires enabled WG, IPv4 `0.0.0.0/0`, a usable explicit IPv4 resolver,
MTU <=1420, and an endpoint UDP port other than 53. The server must forward/NAT;
the user confirmed those settings and an exit matching their Endpoint.

The selected full-mode guard is published before Wi-Fi starts. Linker wrappers
intercept the pinned SDK's real `etharp_output`/`ethip6_output` on STA and AP,
including newly created interfaces. Validation excludes the SDK's `lo` loopback
interface; it cannot reach the radio. A removed WG netif remains blocked at
physical egress even when lwIP falls back after a NULL route-hook result.
LAN/AP, DHCP and local link management remain available. Endpoint underlay
traffic is limited to the resolved address and configured UDP port. Fragmented
physical IPv4 and global/ULA/nonlocal multicast IPv6 cannot bypass that guard.
IPv6 is not tunneled in this release.

Full-mode DNS selects the configured resolver and clears both secondary slots.
An endpoint bootstrap lease permits only the exact configured hostname's
standard UDP A query to the captured underlay resolver: 15 seconds, at most
16 packets per five-minute window. Clock bootstrap permits only a 48-byte
client NTP request to `162.159.200.1:123`: 45 seconds, at most eight packets
per window. Leases close after completion or expiry. Underlay DNS is restored
when leaving full mode; DHCP renewal/reconnect cannot restore unrestricted
router DNS while full mode is selected. An expiring bootstrap lease must not
be mistaken for a new DHCP resolver update.
[Cloudflare NTP addresses](https://developers.cloudflare.com/time-services/ntp/usage/).

The priority-1 CPU1 job requests `https://api.ipify.org/` with the pinned CA
bundle and hostname/SNI verification, bounded connect/handshake/read and a
16-byte maximum IPv4 body. It resolves an IPv4 A record explicitly because
Arduino's generic resolver can prefer STA IPv6 after autoconfiguration; it
does not bind or force a WG socket. A current generation, guarded route,
real peer session and increasing WG TX/RX must accompany HTTP 200. Probe
results from removed/replaced interfaces are ignored. Successful proof is
refreshed after 60 seconds, expires after 180 seconds, and is revoked on a
failed observation, interface removal or peer loss. Verification failure keeps
LAN management/capture available. No credentials are sent to the public probe.
[Public endpoint](https://www.ipify.org/).

The Chinese/English/German UI has a mode selector and the verified exit/age;
only actual successful verification displays the verified full-tunnel state.

The pinned local SDK enables IPv6 autoconfiguration and a default IPv6 route
hook. Consequently, ignoring `::/0` in the parser would not establish an IPv6
no-leak policy. The default hook is weak and returns NULL. In IPv4, a route hook
returning NULL permits the normal route search to continue; this is not a
firewall drop. The source-address hook can also select a physical interface
before the current WG-A destination policy runs.

Primary source: [ESP-IDF v5.5.5 hooks](https://raw.githubusercontent.com/espressif/esp-idf/v5.5.5/components/lwip/port/hooks/lwip_default_hooks.c),
[Espressif lwIP IPv4 route implementation](https://raw.githubusercontent.com/espressif/esp-lwip/2.2.0-esp/src/core/ipv4/ip4.c).
Local header: `esp32s3/include/lwip/port/include/lwip_default_hooks.h`; local
`sdkconfig`: `CONFIG_LWIP_IPV6_AUTOCONFIG=y`, `CONFIG_LWIP_HOOK_IP6_ROUTE_DEFAULT=y`.

## Required implementation and evidence

1. Add an explicit, authenticated, locally confirmed mode trial. Keep the active
   configuration and automatic cancellation/reset recovery. Require an IPv4
   default AllowedIP and a usable explicit resolver; preserve remote-only mode.
2. Keep the resolved endpoint and local LAN/AP on their physical interface.
   Normal non-local IPv4 must select WG, including source-selection handling.
   Removing/restarting the WG interface must leave ordinary WAN traffic blocked
   while the selected mode is still full tunnel.
3. Provide bounded bootstrap DNS/NTP exceptions without making an unrestricted
   secondary DHCP resolver a silent fallback. Restore underlay DNS only when
   leaving full mode. Verify resolver transitions and hostname endpoint renewal.
4. Verify an egress guard at the actual physical output path, including externally
   routable IPv6. Returning NULL from a route hook is insufficient. Preserve
   local management and necessary bootstrap packets. Serialize every netif,
   resolver and guard mutation with the TCP/IP core and network owner.
5. Test ordinary unbound outbound traffic against a public response, actual WG
   data-counter deltas and the expected server exit. A forced-WG socket or a
   non-default CIDR echo cannot prove that the default route works.
6. Exercise key expiry, unavailable peer, Wi-Fi reconnect, changed endpoint IP,
   disabled/full/remote trial cancellation, interrupted trial reset and cold
   start. Mark verification false during failed/expired observations; keep LAN
   recovery. Include DNS/IPv6 negative tests and server NAT/forwarding evidence.

The existing confirmation endpoint intentionally rejects requests arriving at
the WG address. The X390Yoga VPN previously blocked direct LAN access. The user
has since disconnected it, and actual LAN access is working for the recovery
gates. Keep that distinction in future confirmation tests; a workstation route
change is not justification for weakening the endpoint check.

Existing real credentials remain in ignored local files/NVS. WG-B test artifacts
must continue to use them only in RAM and must never compile them into images
or record them in diagnostics.

## Normal-image lifecycle and probe memory

The authenticated OTA of `appliance-full-raii-final.bin` passes **35 production
checks** in `full-mode-final/report.json`: session/CSRF/schema admission,
real unbound HTTPS with expected exit and WG data, cancel/confirm, persisted
full-mode reset, owner peer restart with proof revocation/recovery, cancelled
remote trial returning to verified full mode, and explicit remote-mode restore.
All capture values and actual 240 MHz are preserved.

The failed pre-fix candidate lost **93,952 bytes** across four admitted real
probes, ending with two HTTP -1 failures. Its FreeRTOS worker deleted its stack
before C++ TLS/client destructors ran; HTTP reuse could retain the connection.
The corrected worker disables reuse and exits the object scope before task
deletion. Four new admitted probes all return HTTP 200 and valid public proof;
free heap is **144,780 bytes before and after every probe (zero retained loss)**.
Evidence: `full-probe-memory-red-admitted.json` and
`full-probe-memory-green.json`. This compares actual accepted restarts; the
earlier `full-probe-memory-red.json` contains only one admitted probe and must
not be interpreted as four independent requests.

The backend-gate normal binary: **1,894,384 bytes / 3,342,336 (56.68%)**, static RAM
**103,188 bytes**, SHA-256
`7f75fe16b50eec594d3a332287e116677db1e10636a3176492ceaa276c903325`.
No test endpoint or locally supplied password/key is embedded.

## Final ordinary image and browser/media acceptance

`appliance-full-ui-final.bin` is installed by authenticated LAN OTA and passes
**42 checks** in `full-final-browser-ui/report.json`. Its C++ policy/probe
implementation matches the preceding backend gate; the last changes make the
confirmation panel open once for each new trial and use a shared WG panel title.
The first browser run correctly found the confirmation button inside collapsed
Wi-Fi settings; its failed 23-check report remains in `full-final-browser/`.

The final run checks all four embedded assets, both fixture routes returning
404, Chinese/English/German labels and actual verified-exit text, nine layouts
at 390/768/1366 px, and preserved unsubmitted mode choice across polling.
Actual Chrome submits and locally confirms the full-mode trial. No JavaScript
error, overflow or private browser storage is observed; screenshots are taken
with empty private controls and the Chinese/German final screenshots are
visually inspected. Native file-picker text follows the browser/OS locale.

A new photo and PDM AVI save and download completely. The **1,461,338-byte**
MJPEG 640x480 / 16 kHz mono PCM AVI fully decodes, with **8.133/8.128 seconds
(5 ms difference)**. Fresh and saved JPEGs also fully decode. All 82 earlier
media paths/sizes and 31 values remain, CPU is actually 240 MHz, and camera/SD/
full proof are healthy. The run leaves selected full-mode **revision 71**.

Final normal binary: **1,894,496 bytes (56.68%)**, static RAM **103,188**,
SHA-256 `20517b0dae106d471dce18e354b308e5c9663271c4628cafff21cf2301755fc0`.
BIN/ELF/digest, report, six empty-control screenshots and whole media are in
the ignored local evidence directory. Old passwords/keys are preserved only
in ignored private files/NVS and are absent from the named BIN/ELF audit.

`full-refresh-final/report.json` additionally passes a **190.61-second**
read-only gate on that final confirmed full mode: 59 status observations and
four actual automatic public-proof refreshes, without a forced restart or mode
change. Guard, expected exit, camera/SD, selected revision and uptime remain
healthy. Completed-probe heap observations are **140,376–144,428 bytes**;
the last sample increases by 4,052 bytes instead of retaining a loss. A separate
600.2-second normal serial capture has 4,126 bytes and no panic, watchdog,
stack overflow or legacy frame-analysis errors.

The user has been asked to verify final-image remote login/photo/recording
through `10.8.0.5` from another existing WG peer. The earlier WG-A user/PC
inbound gates are historical evidence, not confirmation of this new full mode.

## Persisted-policy fixture evidence

`full-policy-raii/report.json` passes **50 checks** on the real board. Its
separate 1,900,560-byte fixture exercises the production policy, adds bounded
fault commands, and resets the clock before applying a persisted full-mode
configuration. The normal image exposes neither fixture endpoint.

- Cold boot from clock zero reaches the expected server exit after an actual
  bounded numeric NTP request; full mode uses only the explicit resolver.
- **75 packet cases, zero failures**, invoke the actual guarded STA callbacks
  with a real-output spy: chained buffers, IPv4 fragments/header/UDP validation,
  exact endpoint DNS name/type/transport, lease budgets/expiry, IPv6 WAN/ULA/DNS
  denial, local link traffic and invalid-interface detection. Injected packets
  are intercepted by the spy and are not sent over the radio.
- Removing WG for 30 seconds makes an ordinary unbound HTTPS request fail and
  increases the physical IPv4 denial count by six; a full LAN JPEG still
  decodes. Recreating the peer restores actual public-exit proof.
- A 25-second RAM-only wrong endpoint address revokes proof, preserves LAN
  JPEG and then recovers the original endpoint. A real A lookup of a verified
  hostname alias plus clock reset uses exactly one additional permitted DNS
  query and one NTP request; both leases close and tunnel DNS returns.
- Actual Wi-Fi disconnection and DHCP reconnection restores the guard and exit
  without a secondary router resolver. Cancellation and reset of an unconfirmed
  remote-mode trial restore the selected full mode without promoting its bank.
- All 82 existing media entries and 31 capture values remain. Cleanup confirms
  remote mode and installs the archived normal image with healthy camera/SD/WG.

The 600.2-second serial capture has 13,594 bytes and no panic, watchdog, stack
overflow or legacy frame-analysis errors. Evidence:
`.tools/hardware/full-policy-raii/report.json` and
`full-policy-raii-serial.log`. This fixture establishes actual reconnect and
bounded injected faults; it does not claim external server IP migration, key
expiry, a changed DHCP address, or live IPv6 internet connectivity.

## Verified RAM-only routing/egress spike

Explicit `xiao-appliance-full-tunnel-test` passes **39 real-board/host checks**.
It uses the existing peer and default AllowedIP, never writes NVS, requires
authenticated LAN admission/CSRF, and automatically exits the temporary policy
after three minutes. Only this test environment contains `/api/wireguard/full-test`.

- An ordinary unbound HTTPS request to [ipify](https://www.ipify.org/), with the
  pinned SDK's built-in CA bundle and hostname verification, returns the
  server's public exit matching its Endpoint, as the user specified. Actual WG
  authenticated payload counters increase by **1,145 sent / 3,674 received bytes**.
- The physical STA/AP IPv4 and IPv6 output functions are guarded under the
  TCP/IP core lock. Endpoint UDP transport and LAN management stay physical;
  normal unbound non-local IPv4 selects WG. Explicit source-bound WAN fallback
  is rejected at physical output. Removing the tunnel leaves the guard active.
- Eighteen packet cases invoke the installed underlay output functions with an
  original-output spy: WAN TCP/UDP, physical UDP/TCP DNS including router DNS,
  fragments, chained pbufs, endpoint protocol/port, and global/ULA/link-local
  IPv6 scopes. Zero failures; denied packets never reach the original output.
  This is packet-level IPv6 evidence, not a claim of a live IPv6 WAN connection.
- All three DNS slots are inspected: explicit resolver plus two empty slots
  during full mode, then exact restoration of the original slots on exit.
- A real 45-second tunnel removal produces a failed ordinary HTTPS request
  and seven additional physical IPv4 denials, while fresh local JPEGs fully
  decode. The replacement WG interface is attached to the still-active policy.
- An audio AVI recorded with full routing downloads completely and passes
  whole MJPEG/PCM decode: **1,030,798 bytes, 8.134 / 8.128 seconds, 6 ms difference**.
  All 81 earlier media entries, all 31 capture values and revision 47 remain.

The spike is **not** a boot-safe production mode: it starts from an established
peer/clock, caps MTU at 1420, does not handle newly created physical interfaces
or DHCP changes, and has no endpoint-DNS/NTP bootstrap leases, persisted trial
UX, verification freshness/key-expiry policy or interrupted-trial/cold-start
acceptance. The product verification flag remains false throughout. Same-config
manager retries preserve the guard; only an actual config change exits this
RAM experiment. Do not ship the fixture or call WG-B complete from this gate.

Evidence in ignored `.tools/hardware/`: `wg-full-spike/report.json`, whole clip,
two JPEGs, `wg-full-spike-serial.log`, and `wg-full-spike-candidate.bin`/`.elf`/digest.
Fixture is **1,885,104 bytes (56.40%)**, static RAM **102,852**, SHA-256
`3a3e60b7b96a31c479c6068bf179311aac722680da66ead83225f01ac0268e77`.
The script explicitly restores the normal 1,878,176-byte image and proves the
test route absent, chosen-password login and healthy camera/SD/WG afterward.
Its 400.2-second serial capture contains 6,856 bytes and no panic, watchdog,
stack overflow or repeated missing-config frame-analysis error.
The current-source normal build is also installed by authenticated OTA:
`appliance-full-spike-normal.bin`, **1,878,160 bytes (56.19%)**, static RAM
**102,636**, SHA-256
`1fbb59dfc9e7ce17e039bf1d55d9fc35fda0a63ba7603c195b385ca4cabdbbee`.
`normal-source-installation.json` verifies the chosen account, all four assets,
no fixture route, actual 240 MHz, all 31 values/revision 47 and healthy camera/SD/WG.

```powershell
python scripts/pio.py run -e xiao-appliance-full-tunnel-test
.tools/platformio/penv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_full_tunnel_spike.py --address 192.168.1.181 --fixture .tools/hardware/wg-full-spike-candidate.bin --restore .tools/hardware/appliance-manual-frame-final.bin
```
