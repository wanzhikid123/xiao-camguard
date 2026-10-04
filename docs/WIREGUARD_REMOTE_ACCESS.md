# Product WireGuard remote access (WG-A)

Updated 2026-10-03. `xiao-appliance` now integrates the pinned, patched backend
with the real camera, selected account and last known good network owner.
The final core product gate passes 33 checks, the lifecycle gate 289 checks
and the fault gate 44 checks. The actual Chrome WG UI gate passes 34 checks.
The owner additionally verified remote production login/photo/recording.
Authenticated WG OTA and the 46-check refined Chrome UI pass. Separate
30-minute idle (17 checks) and 47.7 MB WG download (26 checks) now also pass.

## Configuration and lifecycle

The authenticated UI accepts one `.conf` file or bounded text, at most 4096
bytes. The existing parser validates one interface/peer, mandatory keys,
IPv4 CIDRs, endpoint, PSK, keepalive and MTU. Unsupported command fields,
duplicate fields, malformed values and extra peers fail atomically. Raw
configuration and private keys have no read/export endpoint.

HTTP receives a temporary, wiped input buffer without holding the owner
mutex across network waits. `NetworkService` alone parses, stages, changes
interfaces and confirms NVS. Changes keep the active configuration intact,
start a recovery AP, then require the current revision to be confirmed from
the actual candidate LAN socket. An unconfirmed trial times out or is
cancelled; reset never reapplies that pending configuration.

Restart reconnects the interface without replacing NVS or the Wi-Fi underlay.
Disable retains the stored key fields; clear creates a candidate without them.
Only local confirmation makes either change active. Both file import and
individual UI fields use the same bounded parser/trial path.

WG-only edits preserve the working Wi-Fi connection. Wi-Fi address changes
remove the old WG interface and invalidate endpoint resolution. DNS callbacks
use a generation check and a context that is never reused while pending.
Time synchronization precedes WG startup. All backend/netif mutations share
the SDK TCP/IP core lock; the first unconfigured apply avoids taking that lock
before Wi-Fi has initialized lwIP.

## Route policy and limits

WG-A preserves the physical default interface and DHCP DNS. The imported DNS
field is stored but does not replace the resolver in this mode. The imported
IPv4 default route and IPv6 entries are explicitly excluded in remote-access
mode. The new separately selected [WG-B IPv4 full mode](WIREGUARD_FULL_TUNNEL.md)
uses a locally confirmed trial, physical-output protection and ordinary public
HTTPS proof. An authenticated peer session alone does not verify full routing.

The policy routes the configured WG-address subnet and up to eight selected
IPv4 AllowedIPs to the tunnel. It preserves the endpoint on the STA underlay,
physical LAN/AP destinations, broadcast/multicast and existing source-bound
SDK routing. It is cleared before the WG interface is removed. Incoming
AllowedIPs checks remain in the backend; a destination route does not bypass
its source/authentication/replay checks. MTU is applied to the actual WG netif;
this gate used the configuration's default 1420 bytes.

The prebuilt SDK defines a strong `ip4_route_src_hook`. A product-only GNU ld
`--wrap=ip4_route_src_hook` intercepts calls from the IPv4 route translation
unit and delegates preserved cases to the original SDK hook. The actual
unbound ICMP test below verifies destination routing rather than relying on
linker symbols alone. Primary references:
[GNU ld wrapping](https://sourceware.org/binutils/docs/ld/Options.html),
[pinned SDK route hook](https://raw.githubusercontent.com/espressif/esp-idf/v5.5.5/components/lwip/port/hooks/lwip_default_hooks.c).

Status distinguishes configured/enabled, running interface and usable peer
session keys. The status predicate also checks transport age/counter limits
and responder confirmation, so a retained previous-key flag alone cannot
claim a session after expiry. No last-handshake timestamp is fabricated. Plaintext counters
count authenticated data, excluding handshake and keepalive traffic.
The bounded authenticated `/api/wireguard/probe` diagnostic sends three
ordinary, unbound ICMP echoes only to selected WG destinations. It cannot
select the physical LAN, endpoint or the board's own address, and writes no
configuration. ICMP filtering can prevent replies even when a peer is valid.

## Actual product evidence

The owner's ignored configuration was imported with the existing chosen
account and Wi-Fi. No keys/passwords/tokens are recorded in the report.

- Authenticated OTA installed the WG image; camera, SD and PDM recovered.
- Anonymous/CSRF denial, query/NUL/size bounds and absent raw exports passed.
- Invalid command-field import did not change the active NVS revision.
- Real import established authenticated peer-session keys at `10.8.0.5`.
- LAN confirmation promoted the trial and closed its recovery AP.
- An unbound probe to `10.8.0.1` sent three packets and received two echoes,
  with authenticated plaintext deltas of 276 TX / 184 RX bytes.
- Manual audio recording and JPEG remained available with the tunnel active.
- Controlled restart restored the chosen account, selected configuration,
  authenticated peer session, camera, SD and audio.
- The new WG-active AVI (1,710,710 bytes) completely decodes as MJPEG plus
  16 kHz mono 16-bit PCM; video/audio durations differ by 11 ms.
- The prior actual Chrome UI AVI (1,661,282 bytes) also completely decodes;
  its video/audio duration difference is 8 ms.

The first image was 1,815,696 bytes (54.32%). The final image with fields,
restart/clear, AP-lifetime correction and usable-key status is
1,821,248 / 3,342,336 bytes (54.49% of the OTA slot), static RAM 112,156 bytes.
SHA-256: `3f40bec2515338ce0744907d5ca34b919011084c2238612d5a027c3103701b9d`.
Its repeated 33-check core gate passes after authenticated OTA installation.

Twenty integrated WG start/stop cycles each kept manual audio recording
running, established a new session, received authenticated destination-route
echoes, finalized one owned AVI, downloaded and decoded it completely.
Cycles 5, 10 and 15 also forced a nonexistent Wi-Fi trial: the 60-second
rollback restored Wi-Fi/WG without stopping capture. All 289 checks pass;
14 pre-existing media paths/sizes are preserved. AVI duration differences
are at most 84 ms. After-cycle heap changed from 133,472 to 133,320 bytes,
minimum 133,256; PSRAM stayed around 2.87 MB. This bounded run is not proof
that no long-term leak exists.

The final fault gate passes 44 checks: actual selected-key clear and original
reimport, wrong endpoint DNS and random wrong PSK rollback during recording,
sanitized error display, unchanged selected revision, recovery AP closure,
and controlled reset during an unconfirmed WG trial. The old configuration
returns without retrying the interrupted candidate. Both failure AVIs fully
decode. Maximum LAN status latency in the final fault run is 0.216 s.

The final Chrome gate passes 34 checks: real file import and individual field
entry, masked/cleared keys, actual LAN confirmation, restart, clear trial and
cancel, no private web storage/JS exceptions, and no horizontal overflow at
390/768/1366 px. Screenshots taken only after private inputs are empty were
visually inspected. Its report is in `browser-wireguard-final/`.

Ignored artifacts: `.tools/hardware/appliance-wireguard/report.json`,
`decode-report.json`, `wg-active-still.jpg`, both AVIs,
`appliance-wireguard-boot.log`, and archived firmware `.bin`/`.elf`/digest.

```powershell
python scripts/pio.py run -e xiao-appliance
.tools/platformio/penv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_wireguard.py --address 192.168.1.181 --install-first
.tools/platformio/penv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_wireguard_cycles.py --address 192.168.1.181
```

## Remaining acceptance

The owner verified authenticated login, photos and video on the production
page at `http://10.8.0.5/`. This PC also installed a new image using authenticated
OTA through that WG address and logged in after boot. The old experimental
JSON check remains separate evidence.
Twenty integrated lifecycle/recording cycles with three failed-Wi-Fi
recoveries pass. The five-minute audio AVI decodes fully, with 72 ms duration
difference. Earlier idle/transfer attempts were interrupted by Windows LAN
access denial and the former ten-minute download limit. The separate idle
retry now passes 1,800.195 seconds / 169 observations, no reset or continuing
heap loss, with post-idle bidirectional data and camera wake. The distinct
47,667,154-byte WG download completes in 533.55 seconds, matches the existing
reference SHA-256 and decodes fully. The latest path was faster too; the longer
deadline does not imply a fixed throughput. Earlier failures remain archived.
Full tunnel, DNS/leak behavior and public exit proof remain required.
IPv6 tunnel transport remains unsupported; WG-B must also prevent an external
IPv6 bypass. Pinned SDK findings and required evidence are recorded in
[WIREGUARD_FULL_TUNNEL.md](WIREGUARD_FULL_TUNNEL.md).
