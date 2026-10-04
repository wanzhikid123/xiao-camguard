# Implementation progress

Updated 2026-10-04. The supplied v2 plan remains the specification.

The subsequently confirmed BLE, scheduled-photo and MP4 extension is implemented
and installed as the final normal image. Its acceptance state is recorded
in [IMPLEMENTATION_SUMMARY](../IMPLEMENTATION_SUMMARY.md) and behavior in
[the feature guide](FEATURES_BLE_TIMELAPSE_MP4.md). The event-image measurements
below are historical. The new normal image is 2,592,848 bytes (77.58% of the
unchanged OTA slot), static RAM 92,272; both normal and isolated fixture builds
pass the 80% gate. Five codec cases including a 60-second VGA clip fully decode,
53 feature checks and 18 BLE checks with actual Wi-Fi save/rollback pass.
Normal-image installation passes 18 checks, enabled-setting/reboot verification
passes 17, and a five-minute scheduled-photo run passes. The six-hour run was
stopped at the user's request to skip tests over ten minutes; it is not a pass.
Physical iPhone acceptance remains open.

**Core hardware baseline passes. The final appliance remains in development.**

| Phase | State | Evidence / next gate |
| --- | --- | --- |
| 0 Environment / upstream | Ready | Exact upstream SHA and pinned tools |
| 1 PlatformIO baseline | PASS | Real builds/uploads on Arduino 3.3.12 / IDF 5.5.5 |
| 2 Hardware baseline | Core checks PASS | PSRAM, camera/HTTP stream, SD, PDM, AVI/download, app0↔app1 OTA |
| 3 Minimal profile / budget | PASS | Intercom/NVR audio excluded; real preview/audio AVI/OTA preserved; 54.49% slot use |
| 4 NVS / rollback | Module and camera Wi-Fi gates PASS | 78 NVS checks/5 reboots; 37 isolated network checks; 32 final product checks with recording, failed Wi-Fi and interrupted-trial reset |
| 5 WG spike | Bounded hardware gate PASS | Patched pinned PR #52: 20 sessions/data cycles, 3 Wi-Fi reconnects, 40 LAN checks and remote JSON |
| 6 WG-A | Core/lifecycle/fault/UI gates and user remote capture PASS | 33 core, 289 lifecycle, 44 fault, 34 Chrome UI; user remote login/photo/video; PC remote OTA/install/login |
| 7 Authentication / setup | Wi-Fi/account/WG import gates PASS | Enrolled/persisted account; 85 password-rotation checks plus 42 final UI/interface checks; WG import/disable use pending/confirm/rollback |
| 8 Capture / media | Core and refinement gates PASS | Original 44 media checks plus 131 parameter/photo/clock/standby and 44 final-image edge checks; complete JPEG/AVI decode and rename preservation |
| 9 Modern UI | Light three-language UI and account settings PASS | Camera/WG/refinement gates plus actual account success/error and nine account language/width layouts; no private browser storage |
| 10 WG-B | Production mode/lifecycle, physical-policy and final UI/media/refresh gates PASS | 35 normal checks; 50 fixture checks including 75 packet cases/cold clock/bootstrap/Wi-Fi-DHCP return/tunnel-down protection; 42 final Chrome/photo/PDM AVI checks; four admitted public probes retain zero heap loss, four automatic refreshes pass in 190.61 s. Fresh remote-peer confirmation pending |
| 11 Hardening | OTA and actual missing-card/reinsertion gates PASS | Fixed-image 24 absence + 28 restoration checks, zero frame-analysis errors; all 79 original media retained, new photo/PCM AVI whole decode; full/corrupt-card gates remain |
| 12 Validation / docs | Ongoing | Five-minute AVI complete decode, 72 ms difference; 30-minute idle passes 17 checks and 47.7 MB WG transfer passes 26; final failure suite open |

The earlier authorized 2026-10-04 extension added independent 640×480 event clips and
checkbox/multiple/all media deletion. The normal image is 1,916,528 bytes
(57.34% of the OTA slot), with 103,644 bytes of static RAM. Arming is temporary,
starts disarmed after boot and preserves all 31 stored capture values; logout
does not stop monitoring. The 80-check browser/media gate passes on this image, including
real owned-file deletions, manual audio recording, authenticated request guards,
all-date pagination and three languages/nine layouts. Detector integration and
its explicit fixture pass (58 checks, three whole VGA/PDM decodes, 8–21 ms A/V
differences and normal-image restoration) are documented in [EVENT_CAPTURE.md](EVENT_CAPTURE.md).
Final cleanup retains all 89 original media paths/sizes plus three passing event
clips and removes only 44 proven failed-run files. Normal firmware is disarmed
with camera/SD/full tunnel healthy.
Physical object-motion sensitivity remains untested because the user is not
currently available to arrange the camera. The additional BLE, background
transcoding and scheduled-photo plans are now the active extension described above.

## Verified baseline

- Upstream `3c9ae28d04ef77b56b378d4d8dbdfe5674f32ae4`, `upstream-baseline`, feature/xiao-wg-ui and bootstrap bd1ea13.
- Pinned build, equivalent 8 MB dual OTA table and 80% binary-size gate.
- Device Guard launcher fixed using installed isolated UTF-8 Python esptool module; normal PIO exit status propagates.
- Full pre-flash backup, hash-verified uploads and NVS preservation.
- Sense SPI/USB logging corrections, measured 10 MHz XCLK.
- Explicit empty-card FAT32 formatting and full 256 KB write/read; original AVI writes/downloads.
- Manual startup including the second config-vector reload; automatic format/delete disabled.
- Real AP/DHCP/HTTP after fixing stale AP event state; PC Wi-Fi always restored.
- HTTP JPEG plus three MJPEG frames decode.
- Real PDM PCM audio; four physical 880 Hz pulses detected.
- AVI header sample-count and frame-rate rounding regressions reproduced and corrected.
- Post-fix 20 s clip differs by 51 ms; 120 s clip differs by 64 ms; complete video/audio decode.
- Two original OTA updates boot app1 then app0 and restore HTTP/camera/SD/PDM.

The original capture, SD buffering and mux remain in place. Baseline public UI
assets are on SD. The new development profile embeds its login/dashboard assets
in firmware; actual missing-card boot/reinsertion now passes the gates below.

## Outstanding gates

The minimal profile passes. Versioned active/pending NVS is implemented in a
separate library and tested with isolated synthetic fixtures. Its new single
owner network manager also passes 37 actual Wi-Fi/LAN/AP checks, including a
failed-network rollback, interrupted-trial reset and runtime reconnect. The
root setup/settings/recovery flow now uses it and passes real failed-network
rollback during recording plus interrupted-trial reset. The patched WG backend
is integrated and passes the first 33-check WG-A product gate. Existing
server/config and 2.4 GHz Wi-Fi were supplied privately and tested; no further
credential request is needed.

WireGuard text import and salted admin verification/sessions now pass isolated
hardware checks. HTTP login is asynchronous: the pinned SDK takes approximately
35 seconds for 600,000 PBKDF2-HMAC-SHA256 iterations; 160 HTTP probes remain
responsive during that work. Real credentials were not embedded in test builds.
The camera integration then passed 67 real checks. Preview and recording run
during password work; logout closes the existing stream in 0.094 s. 198 HTTP
probes have a maximum 0.135 s response; concurrent login takes 45.701 s. Legacy
mixed command/config routes are absent in the new development profile. See
[APPLIANCE_HTTP.md](APPLIANCE_HTTP.md).
The user chose to supply their own admin password in ignored
`.tools/private/admin-password.txt`; their chosen value is now saved there.
Production enrollment has completed through the new first-run HTTP flow.
The selected account and Wi-Fi survive a normal reset. Its final 32-check
product recovery run passes, including continued recording during failed Wi-Fi.
The appliance is on LAN `192.168.1.181`; its recovery AP closes after stable
connection. Existing baseline firmware remains archived privately. Photo/media
passes 44 real-account checks, with complete JPEG and two audio AVI decodes.
Authenticated LAN OTA passes 64 checks including actual boot-health rollback.
WG-A now passes actual import, session, destination-route data, camera/audio,
20 lifecycle / 3 Wi-Fi recoveries, fault rollback, field/restart/clear UI
and reset checks. The user verified real remote login, photo and video at
`http://10.8.0.5/`; this PC has now installed a new image by authenticated WG
OTA and signed in after boot. The independent 30-minute idle and 47.7 MB WG
download gates now pass; earlier host restriction/deadline failures remain archived.
See [WG-A](WIREGUARD_REMOTE_ACCESS.md).

The user's new image/audio/UI/performance requirements now pass 131 real-board,
46 actual Chrome and 44 focused final-image checks. The light Chinese/English/
German UI, eight photo resolutions up to 2048×1536, 31 persisted parameters,
fourfold default audio gain, scheduler CPU/heap metrics, safe media renaming,
supported 80/160/240 MHz clocks and standby/wake are deployed by WG OTA.
HD/UXGA audio videos also decode fully, with actual rates around 9/7 FPS in the
test scene. See [PRODUCT_REFINEMENTS.md](PRODUCT_REFINEMENTS.md).

Actual microphone off/idle now stops the PDM RX channel; reenable and idle
recording wake pass 37 additional hardware checks, with full AVI decodes and
preserved media/parameters. A later 50-check Chrome gate covers the translated
off-scope label and clock-aware login hint. The 80 MHz diagnostic login takes
168.09 seconds; the former 120-second client and 150-second UI deadlines were
too short. Server/UI ticket waiting was increased without changing KDF strength,
and revised real low-clock browser verification is a separate gate.

Subjective speech/noise, physical full/corrupt SD, external full-mode server
migration/key expiry and fresh final-image remote-peer confirmation remain
separate acceptance items. The 20 integrated record cycles,
WG reconnect/routing and safe space-reserve simulation have passed. The independent WG 301/counters
do not complete WG-B. No remote publication is claimed. See [TEST_REPORT.md](TEST_REPORT.md).

The explicit missing-mount fixture passes 24 real-board checks and is no longer
running. Management/auth/WG/JPEG survive the absent mount and reset, SD operations
fail explicitly, and WG OTA restores the normal image with unchanged media and
parameters. This does not establish physical removal/contact/corrupt-card behavior.
See [STORAGE_RECOVERY.md](STORAGE_RECOVERY.md).

The final refinement image completed 1,800.195 seconds of idle with 169 WG
observations, no reset, stable post-standby heap (159,640–159,644 bytes) and
PSRAM minimum 5,717,840. Maximum WG status latency was 1.367 seconds; fresh
bidirectional data and JPEG wake then passed. A separate whole 47,667,154-byte
WG transfer finished in 533.55 seconds with matching SHA-256 and full decode.
These are 17 and 26 checks respectively, with separate retained reports.

The 80 MHz browser follow-up exposed a standby camera failure and a separate
WG cleanup panic during authenticated recovery. An actual lwIP bound-TCP
fixture reproduces the latter. The fixed fixture passes 32 checks across three
teardowns, with new sessions/data and no teardown reboot; normal authenticated
OTA recovers healthy camera startup from the red run's crash guard. Core-lock
ownership, crypto and the guard remain unchanged. Low-clock camera/browser
validation and restoring the normal firmware remain the next distinct gates.

The earlier normal 1,882,672-byte image passes 27 real 80 MHz mobile
Chrome checks, including login/still wake and original values/240 MHz recovery.
The broader mixed-clock run passes 80 MHz still/settings/audio wake (complete
AVI, 11 ms difference) but fails the following 160 MHz wake. That report remains
failed; normal authenticated restart restores healthy camera/240 MHz with no
WG panic. Diagnostics and a later production repeat show that merely changing
the clock or teardown order does not reliably fix this case. Those failed
reports remain separate from the browser and WG teardown gates.

The OV3660 sensor-power-down implementation passes the same mixed-clock
sequence twice (102/101 checks): 18 real standby wakes across all three clocks,
checked sensor state/fresh frame starts, and six complete MJPEG/PCM decodes
with 3–29 ms duration differences. Existing media, original values and actual
240 MHz are restored, without reboot. PDM stops and Wi-Fi modem sleep remains;
camera driver/buffers stay allocated. Its 1,883,568-byte normal image contains
no debug probe or WG teardown fixture. The separate 44-check gate saves,
downloads and fully decodes 2048×1536 photos from standby at all three clocks.
The same image's final low-clock mobile page passes 25 Chrome checks, including
checked sensor/fresh-frame wake, status/button updates and restoration. Login
takes 101.53 seconds; password-work status latency is at most 177 ms. Physical
card baseline passes 11 checks with source-matching assets and 78 media entries.
Actual absence/reinsertion remains a separate required hardware action; the
user has been asked to power off before removal. Its baseline remains archived;
the later account image has a separate refreshed baseline below. WG-B and
physical card faults remain open; the root v2 plan is not complete.

Administrator password changes pass **85 real-account checks**: originating
logout cancellation, wrong-current rejection, real Chrome submission, two
distinct session revocations, normal restart persistence, and a 203.57-second
rotation at 80 MHz. The 240 MHz Chrome rotation with concurrent recording takes
94.22 seconds; the whole MJPEG/PCM AVI decodes with a 4 ms difference. Fifteen
status samples take at most 141 ms. The chosen password, all 31 values and actual
240 MHz are restored and reboot-checked; all 78 preexisting media entries remain.
The temporary recovery password exists only in the ignored private directory.

The final **1,896,416-byte** image (56.74%, static RAM 113,804) is installed by
authenticated LAN OTA and passes **42 final account UI/interface checks**.
All four assets match source; three language titles/live wrong-password errors,
nine widths, local policy/mismatch rejection, cleared password fields, one
expensive request, selected revision 47 and healthy camera/SD/WG pass. The
core auth/config code is identical to the 85-check candidate; only the UI's
unconfirmed-result wording differs. Final screenshots are visually inspected.
Its separate physical-card baseline passes 11 checks, preserving 79 media
entries, all 31 values and selected revision 47/CPU 240.

The user then physically removes the card with power off. Functional missing-card
checks pass, but serial replay finds 814 legacy light-analysis errors because
`colorDepth` was initialized only by optional SD config. The manual appliance
does not use motion/light/PIR results; skipping that work fixes the error without
changing the original baseline. Normal missing-card OTA installs the fix, and
the repeated **24-check** gate plus 180-second serial/replay pass with zero such
errors. Auth/config/UI values and selected revision 47 are unchanged. The new
**1,878,176-byte** normal image uses **102,636 static RAM bytes**. Actual power-off
reinsertion then passes **28 checks** with all 79 original media paths/sizes,
31 values and revision 47 intact. New photo and real PDM AVI save, whole readback
and full decode pass; 1,075,658-byte AVI durations are 8.277/8.288 seconds (11 ms).
The 120-second serial capture has zero old frame-analysis errors. Firmware and
network settings are unchanged through restoration; test media are retained.
At this earlier card gate, WG-B and genuine full/corrupt-card failure behavior
remain open; the production WG-B gates below subsequently pass.

The isolated **1,885,104-byte full-routing fixture** passes 39 checks, including
an ordinary certificate-verified HTTPS request with the expected server exit
and 1,145/3,674-byte actual WG payload deltas. Eighteen physical-output packet
cases pass. Removing the tunnel for 45 seconds blocks the ordinary public
request while LAN JPEG remains usable; reconnect attaches the replacement
interface, and all three original DNS slots are restored on exit. The whole
audio AVI decodes with a 6 ms duration difference. All 81 original media,
31 values and revision 47 remain, and authenticated OTA restores the normal
image with no fixture API. This proves the bounded spike only; no persisted
full mode or boot/bootstrap/freshness/lifetime proof is claimed. See
[WG-B boundary](WIREGUARD_FULL_TUNNEL.md).
The post-spike normal 1,878,160-byte build (56.19%, static RAM 102,636) is
separately installed and checked: source-matching assets, no spike API, original
account/values/revision 47, actual 240 MHz and healthy camera/SD/WG. Fixture
serial capture has no panic/watchdog/stack or old frame-analysis errors.

Production IPv4 full mode subsequently passes **35 normal lifecycle checks**
and **50 physical-policy fixture checks** with 75 output packet cases. Actual
unset-clock boot, bounded endpoint A/NTP, absent peer with physical WAN denial
and LAN JPEG, wrong RAM endpoint recovery, Wi-Fi/DHCP reconnection and interrupted
trial reset pass. All 82 original media and 31 values remain. The TLS worker's
real four-probe leak is reproduced (93,952-byte loss) and fixed by scoped cleanup
before task deletion; four admitted corrected probes retain zero heap loss.

The final **1,894,496-byte** normal image (56.68%, static RAM 103,188) is installed
by authenticated OTA and passes **42 Chrome/photo/PDM AVI checks**. Three
languages/nine widths and an actual browser-confirmed full trial pass. The
confirmation panel now opens once for each new revision. Complete audio/video
decode differs by 5 ms, all prior media/values remain and actual CPU is 240 MHz.
The selected full-mode revision is 71. Normal firmware has neither fixture API;
600.2-second final serial capture (4,126 bytes) has no panic/watchdog/stack or
legacy frame errors. Automatic-refresh and fresh remote-peer results are recorded
in [WG-B](WIREGUARD_FULL_TUNNEL.md); genuine full/corrupt-card and subjective
audio behavior remain explicit unverified boundaries.
