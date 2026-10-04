# Decisions

## Local tooling on Windows

The system has Python 3.14.5. An isolated Python 3.12 interpreter created by uv
was blocked by Windows application control (error 4551). No policy was changed.
PlatformIO 6.2.0 was installed into ignored `.tools/python`. PowerShell's script
execution policy also blocks the optional `.ps1` wrapper, so the supported entry
point is `python scripts/pio.py`. It does not require a policy change.

During an earlier session change from unrestricted to managed permissions, the original
local installation was not readable by the new restricted process. Already
installed Python modules were copied, through normal permitted file reads, from
the existing PlatformIO environment into workspace `.tools/pydeps`. The launcher
uses this as a fallback. Its own core/cache root is `.tools/platformio`. It uses
UTF-8 for German serial descriptions to avoid a GBK encoding failure. Unused
local environments/caches are ignored and were not deleted.

No access policy or ACL was changed. The user subsequently installed the pinned
GCC 14.2 compiler and tools. No unsuitable old compiler was substituted.

Device Guard then rejected the esptool launcher at `bootloader.bin`. Two launcher
probes failed; the same venv's Python module probe succeeded with esptool 5.4.0.
Use the supported module entry point via a repository extra script, including
the factory hook that otherwise calls the launcher directly. Python `-I` avoids
Core fallback packages on PYTHONPATH shadowing the installed tool. Python
`-X utf8` is also required because `-I` ignores PYTHONIOENCODING and Windows GBK
cannot encode flash progress bars. Full image generation and real serial uploads
now pass. See BUILD.md for the guarded hook and evidence.

The earlier restricted process could not download the fallback's SCons URL.
`scripts/build_cached.py` retains a build-only fallback using installed SCons
4.11.1. As of 2026-10-03 the normal Core installation is readable and normal
build/upload commands work. No policies or ACLs were modified.

## Build source layout

Keep the sketch at the root and compile upstream `src/*.cpp` with include paths
for both directories. Do not move or rewrite the recorder to accommodate PIO.

## Hardware gates

The user first requested computer-only work, then explicitly confirmed the XIAO
and SD were connected and authorized testing. COM3 was verified against the
ESP32-S3 identity/MAC and a full flash backup was taken before actual uploads.
Hardware checks are reported separately from host checks; core hardware baseline
checks now pass: AP/HTTP preview, real audio AVI and two OTA transitions. The empty card was subsequently formatted FAT32 with explicit authorization and tested.

## Platform selection is compile-verified

Release 55.03.312-1 was selected after inspecting pioarduino's release and actual
package manifest. It pins Arduino 3.3.12 and IDF 5.5.5. The complete baseline now
compiles/links, generates valid images and boots on the target. Independent and original HTTP camera captures, physical microphone tone capture, real AVI and two OTA transitions pass; final stress acceptance remains open.

## Early OTA planning without altering recording

The selected board already references Arduino's `default_8MB.csv`, with two
0x330000 app slots. The checked-in table is byte-for-byte equivalent in values.
Selecting it explicitly makes budget checking reproducible. No larger app slot
or removal of OTA was used to conceal a size issue. The current audio binary is
1,823,584 bytes (54.56% of each slot) after XIAO/AVI adaptations and passes the budget.

## Feature gate and independent preparation

The plan requires clean camera baseline verification before product feature
integration. Core hardware verification has passed. Auth, NVS, embedded recovery UI and camera-integrated WireGuard remain future product phases.
Independent WG experiment code and release validation tools are preparatory work.
The experiment is intentionally excluded from the camera source filter.

## Measured Sense adaptations

Keep a board-specific SPI STORAGE adapter with CS21/SCK7/MISO8/MOSI9 and do not use CS21 as a lamp output. The actual card's exFAT volume was unsupported; the owner confirmed it was empty and requested one-time formatting. An isolated utility used FM_FAT32, checked the board MAC and previous volume, and required a USB command. Normal firmware keeps auto-format/delete disabled.

USB app logs must use native Serial rather than IDF UART stdout. The connected OV3660 produced an undecodable 20 MHz JPEG with intact transfer MD5; 10 MHz output decoded after repeated resets. Apply 10 MHz at XIAO startup. Keep the in-memory config vector consistent, since prepRecording reloads it before task creation and could otherwise re-enable motion defaults.

Use an optional xiao-audio-baseline comparison preserving capture/audio/mux logic; real recordings required narrow AVI header corrections. Public upstream assets were provisioned through an isolated utility to avoid requiring internet for this baseline. This is preparation, not the final embedded recovery UI. AP connectivity now passes after checking actual driver mode rather than trusting a stale AP event flag.

## Password work and HTTP isolation

Use SDK mbedTLS PBKDF2-HMAC-SHA256 with 600,000 iterations, a random 16-byte salt
and a 32-byte verifier. Measured CPU1 priority-one work takes ~34 seconds, so HTTP
login returns a bounded ticket and verifies on a worker. CPU0 idle/network tasks
remain schedulable; no watchdog was disabled or changed. A portable-software
comparison was slower and discarded; no additional password crypto is vendored.

Keep sessions in RAM; require CSRF and matching browser Origin for mutations.
Verifier assignment invalidates sessions and unfinished login results. The
adapter's 42-check HTTP gate passes. The root camera integration now enforces
it at each registered API and inside the MJPEG frame loop; initial setup,
settings/media and authenticated OTA remain separate integrations.

## Embedded camera access boundary

Use a new `xiao-appliance` development profile. Register an explicit API set,
rather than permitting mixed legacy GET commands, settings dumps, file upload,
WebSockets or SSE. Firmware contains the login/dashboard assets; no SD/CDN is
needed to render them. The current profile is locked without a versioned active
admin account and does not create one automatically.

Long previews must check the session repeatedly, without extending idle expiry,
and wipe the request's token on completion. Actual logout ends an existing
stream in 0.094 s. Protect record start/stop with CSRF and matching Origin, keep
capture/file finalization on the original workers, and refuse restart until
the AVI is finalized. Camera/preview/recording remain available during password
work; concurrent login currently takes about 46 seconds.

Reject nested login JSON before calling the general parser. The permitted
schema is one flat password field; a 512-byte length bound alone does not bound
parser recursion. Wipe parsed flat strings on both valid and rejected requests.
Reject decoded NUL escapes while retaining properly escaped literal backslashes.
Control requests have empty bodies and no query; unexpected arguments cannot
change the action selected by the route.

Keep the old gateway-ping reconnect/watchdog owner out of the new profile.
The forthcoming single network manager owns reconnect/trials/rescue/WireGuard.
The currently always-enabled development AP is not the final recovery policy.

## Measured network and AVI regressions

Authenticated OTA uses the existing dual-slot Update library and upstream
camera/timer shutdown prerequisite. Require a complete SHA-256 before boot
selection, keep the network/auth tasks responsive, and finish async previews
before deleting their workers. Defer Arduino's early pending-image acceptance
until real recovery HTTP responds; validate the SDK fallback with an explicit
boot-health rejection fixture. LAN hardware checks pass; WG OTA remains a
separate acceptance item. See [OTA.md](OTA.md).

A fresh AP scan and DHCP/HTTP checks pass after correcting stale AP state. Windows can reconnect to its original network during a board restart, so the bounded host harness explicitly reconnects the test AP before post-restart validation.

Real 20 s AVI exposed an audio dwLength byte/sample unit error. A subsequent low-frame-rate clip exposed integer FPS rounding and duration drift. Preserve the mux; write the correct PCM sample count and measured rational video time base in the header. After the fixes, whole-file decode and a 120 s recording pass, with 64 ms audio/video duration difference. Speech quality and the final stress suite remain distinct acceptance items.

## Capture controls and idle standby

The existing capture task owns sensor changes, still-photo resolution changes,
and camera standby/wake. Recording requests are refused during a change; changes
are refused during recording/finalization. The original AVI writer/mux is reused.
Photo resolution and JPEG quality are applied temporarily, then video settings
are restored before video capture continues.

Public capture values use a separate versioned `camcapture` NVS envelope with
two CRC-checked banks and a selector written last. This deliberately preserves
the already deployed `camcfg` account/network schema and its OTA compatibility.
Only the capture task writes its namespace. Failed saves restore the previous
sensor/clock settings; an unknown selected format blocks writes.

The pinned camera header added `jpeg_buffer_size`. Initializing the complete
`camera_config_t` is required: idle reinitialization uses the capture task stack,
where an uninitialized tail field can produce a different allocation than cold
startup. A standby still request also needs a bounded longer frame wait.

CPU busy percentages use two existing idle-task runtime counters, with a two
second window and no stack scans or calibration loop. They are estimates based
on scheduler accounting. Internal allocatable heap and external PSRAM have
separate denominators; neither represents all physical SRAM.

Standby preserves network/auth/WireGuard owners. When no preview, photo or
recording needs it, the capture owner powers down the identified OV3660 sensor
while retaining its driver/buffers; other sensors use the deinit fallback.
The audio owner stops/restarts its own PDM input. Wi-Fi modem sleep is enabled
in standby and disabled for capture. No deep sleep or measured power saving is
claimed. The later investigation below records why full teardown was replaced.

Mic off also releases PDM while the camera is awake; its audio owner publishes
an atomic RX-channel running state and retries stop/start as needed. Silent AVI
is outside the existing recorder contract, so mic off is labeled preview/photos
only and record start is refused until the microphone is enabled.

Actual 80 MHz authentication measured 168 seconds, beyond both the former
120-second diagnostic client and 150-second browser waits. The single-job
ticket lifetime is now 300 seconds and the browser wait 280 seconds. Keep
600,000 PBKDF2 iterations and the same session/admission limits; lowering a
performance setting must not reduce password-verification strength.

## Low-clock wake and WG teardown investigation

The extended real 80 MHz browser gate exposed a distinct camera failure after
standby: status reported `cameraReady=false`, `startupFailed=true`, while the
network and SD remained available. Capture settings then returned
`503 camera_unavailable`, including the attempted return to 240 MHz. The
wake path initializes the camera at its stock 240 MHz clock before restoring
the selected 80/160/240 MHz clock. The normal image now passes 27 real Chrome
checks at 80 MHz after standby, including actual still wake and 240 MHz
settings recovery. The broader low-clock still/settings/audio gate is separate.
That broader run then passed all three 80 MHz wakes and a complete audio AVI
(11 ms duration difference), but failed the next 160 MHz still wake and could
not save the recovery values while `startupFailed` was set. It is not a pass.
A normal authenticated restart recovered the camera and all original values at
240 MHz, without the former WG teardown panic. Targeted first-frame/init-error
instrumentation is being used to distinguish driver/resource/clock causes.

The instrumented mixed-clock run reproduces the same failure. In contrast,
a cold-start 160 MHz still-only run passes 15 checks. At the mixed-run failure,
`esp_camera_init` returned success, the sensor was identified as OV3660,
initialization was actually running at 240 MHz, and free heap/PSRAM were about
135 KB/2.87 MB. The failure was the first-frame acquisition, rather than a
reported sensor-probe or allocation failure. This distinguishes the failing
stage; it does not yet identify the driver cause. The next single-variable
candidate waits for the audio owner to release its real PDM RX channel before
camera teardown. Reports and serial logs are retained separately under
`low-clock-wake-diagnostic`, `low-clock-wake-minimal`, and
`low-clock-wake-audio-order` in the ignored hardware evidence directory.

That one-variable candidate passes the original mixed-clock sequence: 58
checks, six real standby wakes, fully decoded 80/160 MHz audio AVIs (4/5 ms
duration difference), no reboot, and all original values/media preserved.
Stopping PDM before camera teardown is retained; merely changing the camera
initialization clock did not fix the mixed case. The production follow-up
removes the debug probe and broadens the same sequence to 240 MHz, but fails
the same 160 MHz step again. Thus the single successful candidate run is
insufficient evidence of a reliable teardown fix. Failed
wake/expired still requests also cancel their pending capture flags, so a
failed request cannot cause continuous wake retries or block authenticated
restart indefinitely. The existing crash-loop guard remains unchanged.

The next standby implementation uses the OV3660's documented software power
down bit (`SYSTEM_CTROL0`, register `0x3008`, bit 6), through the pinned driver's
public masked `set_reg`/`get_reg` functions. Only the identified OV3660 receives
this register operation; other sensors retain the full-deinit fallback.
PDM RX stops and Wi-Fi modem sleep remains enabled. Camera driver/buffers stay
allocated, avoiding the repeatedly failing recreation path. Successful standby
and wake require register readback. Wake drops pre-standby queued frames and
requires a driver frame-start timestamp after the wake request before publishing
capture again. The authenticated status exposes the checked sensor sleep state
and last fresh-wake frame timestamp. This trades idle free PSRAM for reliable
reuse; previous deinit-mode memory measurements remain historical. No wattage
claim is made. The original mixed-clock sequence now passes twice, with
102/101 checks: 18 real standby wakes across 80/160/240 MHz and six fully decoded
MJPEG/PCM clips. Duration differences are 5/3/3 and 5/4/29 ms. Original media,
capture values and the actual 240 MHz clock are restored after each run; no
reboot is observed. These results replace the single-pass teardown candidate
as the standby regression evidence. Separate high-resolution photo and real
low-clock page checks follow.

Primary sources: [OV3660 register definition](https://raw.githubusercontent.com/espressif/esp32-camera/3fb41a99d61a853313d1cd5543ebf2c109ef7c0e/sensors/private_include/ov3660_regs.h),
[masked sensor operations](https://raw.githubusercontent.com/espressif/esp32-camera/3fb41a99d61a853313d1cd5543ebf2c109ef7c0e/sensors/ov3660.c),
and [driver frame-start timestamps](https://raw.githubusercontent.com/espressif/esp32-camera/3fb41a99d61a853313d1cd5543ebf2c109ef7c0e/driver/cam_hal.c).

The authenticated recovery restart exposed a separate WG teardown panic.
The archived installed ELF resolves the stack from `netif_remove` through
`tcp_netif_ip_addr_changed`, `tcp_abort`, TCP reset output and
`wireguardif_output` into `peer_lookup_by_allowed_ip`; the fault address is
`0x000000b0`. The adapter freed `netif->state` before interface removal, although
lwIP can synchronously call its output while aborting bound TCP connections.
The regression fixture prepares such a PCB using the actual pinned lwIP stack,
under the same core lock, rather than simulating the callback in a host test.
Its explicit environment must never be the final deployed firmware. Reports
and serial evidence remain separate from the previous lifecycle checks, which
did not keep this bound TCP reset path active during shutdown.

The original-order fixture did reproduce the same null-state panic, but its
subsequent camera recovery assertion failed because the existing crash-loop
guard became active. The red report remains failed. Recovery uses the normal
authenticated OTA/controlled-restart path; neither the guard nor RTC memory is
patched to make the test pass. The updated adapter removes the interface while
its device context is still alive, then cancels the timer, removes UDP and frees
the context, all within the existing TCP/IP core lock. Peers are cleared before
removal, so reset output cannot transmit with discarded keys. The same ordering
is used for partial-start cleanup.

The synchronous cleanup contract is visible in the pinned primary sources:
[interface removal](https://raw.githubusercontent.com/espressif/esp-lwip/2.2.0-esp/src/core/netif.c)
and [TCP address-change handling](https://raw.githubusercontent.com/espressif/esp-lwip/2.2.0-esp/src/core/tcp.c).

## WG-B output-guard compatibility spike (2026-10-04)

Default routing alone cannot prevent source-bound physical fallback or IPv6
egress. The explicit RAM-only environment therefore wraps actual physical
`netif::output`/`output_ip6` callbacks and serializes both those hooks and route
policy with the pinned TCP/IP core. A removed WG interface detaches its pointer
without removing the physical guard. Same-config owner retries preserve this
guard; config changes/end-of-test explicitly restore original callbacks/DNS.
The test environment automatically exits after three minutes and never writes
NVS. Normal builds contain neither its code nor HTTP test routes.

Eighteen packet cases exercise the installed output callbacks with an original
output spy, while separate ordinary unbound HTTPS and actual tunnel removal
prove real routing/public exit and physical rejection. Thirty-nine checks pass,
including intact LAN JPEG and full audio-AVI decode. This is a compatibility
gate, not a boot-safe persisted full-tunnel mode. Bootstrap, dynamic interface/
DHCP handling, verification freshness and product trial UX remain separate work.
See [WG-B evidence and boundary](WIREGUARD_FULL_TUNNEL.md).

## Production full-mode candidate (2026-10-04)

Use an explicit mode-only locally confirmed trial. Require default IPv4
AllowedIPs, explicit usable IPv4 DNS and MTU <=1420; import continues to start
in remote-access mode. Publish egress protection before radio startup, with
global STA/AP physical-output wrappers rather than a NULL routing result.
Exclude the SDK's internal loopback from physical-interface validation.
Keep LAN/DHCP/local link control and tightly bounded endpoint-DNS/numeric NTP
bootstrap. DHCP cannot silently replace the selected resolver. Preserve the
captured secondary resolver slots when a bootstrap lease expires.

Public proof uses a normal unbound IPv4 HTTPS request with CA bundle and
hostname/SNI validation. Arduino's generic resolver can prefer STA IPv6;
request an A record explicitly for this IPv4 milestone. Generation tags reject
completed work from a removed interface. Success refreshes at 60 seconds and
expires at 180 seconds; failures or peer loss revoke the verified state.

The first repeated candidate exposed a TLS lifetime bug: `vTaskDelete(nullptr)`
does not unwind local C++ objects, and HTTP reuse can keep the TLS connection
alive after `end()`. Four admitted real probes dropped free heap from 141,664
to 47,712 bytes; the last two returned HTTP -1. The fix disables connection
reuse and ends the TLS/client object scope before publishing completion and
deleting the task. The old RAM spike worker receives the same cleanup.
Red evidence: `.tools/hardware/full-probe-memory-red-admitted.json`.
The corrected fixture passes 50 checks (including 75 packet cases), and the
normal image passes 35 mode/lifecycle checks. Four admitted corrected probes
return HTTP 200 with the expected exit; free heap stays 144,780 bytes before
and after every probe. Green evidence: `full-probe-memory-green.json`.
The final normal image additionally passes 42 actual Chrome/media checks,
including real mode confirmation, whole photo/PDM AVI decode and unchanged
prior media/values. The confirmation panel opens once for each new trial
revision; the failed folded-panel run is preserved separately. A separate
190.61-second read-only gate observes four automatic public-proof refreshes,
healthy guard/exit/camera/SD/revision/uptime and no retained heap loss. Fresh
remote-peer confirmation remains separate from these local gates.
