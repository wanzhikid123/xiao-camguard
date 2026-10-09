# Test report

The 2026-10-09 NTP/UTC-versus-Berlin clock correction, normal firmware install,
saved-photo and short-video validation are tracked in [CLOCK_SYNC.md](CLOCK_SYNC.md).

The 2026-10-09 clock overflow, block JPEG/I420 and resource admission regression
is tracked in [CODEC_OPTIMIZATION.md](CODEC_OPTIMIZATION.md).

The 2026-10-09 video limit, single event-session AVI, visible AVI clock and
pure idle H.264 conversion are tracked in [EVENT_AVI_CLOCK.md](EVENT_AVI_CLOCK.md).
The initial idle failure is recorded in [EVENT_SESSION_FIX.md](EVENT_SESSION_FIX.md).
The 2026-10-04 results below are historical evidence for the earlier firmware.

BLE / interval photos / MP4 is implemented. Current evidence and
remaining checks are tracked in [IMPLEMENTATION_SUMMARY](../IMPLEMENTATION_SUMMARY.md).
The event-image results below describe the previous accepted image. The new
normal image is 2,592,848 bytes / 77.58%, static RAM 92,272, SHA-256
`1e5932906b354fb09896034c7ee7fba1eaa009e20b01a30b21c6685e9ab649ed`.
Real codec matrix/60-second VGA, 53 feature checks, 18 secure BLE/Wi-Fi checks
and three USB PIN checks pass; latest queue/browser/release evidence is listed
in the implementation record. Normal installation passes 18 checks, final enabled
setting/reboot verification passes 17, and the five-minute soak saves six real 3MP
photos with no skips/failures. The six-hour run was stopped at the user's request
to skip tests over ten minutes; it is not passed. Physical iPhone checks remain
open, and construction of safety guards is not reported as physical fault testing.

## Previous extension: independent events and bulk media deletion

The final normal `appliance-events-final.bin` is **1,916,528 bytes / 57.34%** of
the 3,342,336-byte OTA slot, static RAM **103,644**, SHA-256
`18b9fb47376bca344a0de857c89ab0227435efc692c646914315e26d3ba67682`.
The explicit event fixture is 1,917,760 bytes / 57.38%, static RAM 103,652;
both normal and fixture builds pass. Fixture and production evidence remain
separate; the normal build has no detector substitution route.

The production Chrome/HTTP/media gate passes **80 checks**: source-matching
assets, boot disarmed, real JPEG detection, completed sensor restoration before
reporting disarmed, session/CSRF/Origin/schema/path/existence guards, recording
and monitoring conflicts, three languages/nine widths, checkbox selection,
cancel without mutation, and deleting 24 owned test files. The independent
41-file/21-date pagination model does not delete real SD files. All 133 prior
media entries and 31 capture values remain unchanged in this gate. A real
manual MJPEG/PDM AVI fully decodes: 6.323/6.336 seconds, 13 ms difference.
Evidence: `event-media-browser-final/report.json`, three screenshots and
`manual.avi`. The earlier 77-check pass remains archived separately.

The controlled event integration passes **58 checks**, including six on-board
detector cases, static/noise/brightness rejection, logout/idle behavior, continued
and brief events, three whole 640×480 MJPEG/PDM decodes (8/21/15 ms differences),
all stored settings, actual sensor restoration and final normal OTA/full-tunnel
health. The fixture has actual camera/audio/SD recording with substituted small
detector input. Original-file reconciliation and failure history are detailed
in [EVENT_CAPTURE.md](EVENT_CAPTURE.md). Physical motion
sensitivity is deferred at the user's request, not established by synthetic
detector input. Earlier phase results below are historical and do not replace
the latest extension's build, fixture and browser reports.

Final reconciliation passes: all 89 preexisting paths/sizes and all other media
are preserved; 44 proven failed-run owned clips are deleted (20+20+4), leaving
the 89 originals and three verified event clips. Final normal firmware remains
disarmed with camera/SD/full tunnel healthy. Cleanup and mobile toolbar evidence:
`events-red-{owned,cleanup}.json`, `events-final-visual.json` and three PNGs.
The final 600.1-second/7,365-byte serial capture contains the three event closes,
normal restoration and idle operation, with zero panic/watchdog/stack-canary/
heap-corruption matches. Evidence: `events-final-green-serial.log` and
`events-final-serial-summary.json`. Canonical and archived normal binary hashes
match. Staged-source and named firmware/report/log private-value checks pass
against the six supplied values; private artifacts remain ignored.

The connected XIAO Sense passes the core hardware baseline: original HTTP camera preview, manual AVI with PDM audio, SD writes/download and two OTA slot transitions. Final product features and stress acceptance remain separate gates.

| Check | Result |
| --- | --- |
| Upstream / build | PASS: pinned upstream 3c9ae28, Arduino 3.3.12 / IDF 5.5.5, Core 6.2.0 |
| Device Guard workaround | PASS: installed Python esptool 5.4.0 module creates images and uploads; Windows policy unchanged |
| Launcher failure propagation | PASS: nonexistent environment returns 1 after SystemExit(main()) |
| Current audio firmware budget | PASS: 1,823,584 / 3,342,336 bytes = 54.56%; static RAM 99,592 |
| Minimal firmware / budget | PASS: current restored build 1,821,376 bytes = 54.49%; static RAM 99,584; original minimal evidence used 1,821,360 bytes |
| Partition table | PASS: two app slots, valid alignment/boundaries, automatic 80% limit |
| Host checks | PASS: 9 unit checks, Python syntax checks, git diff whitespace check |
| Target / preservation | COM3, ESP32-S3 rev 0.2, MAC e0:72:a1:fb:4c:08; full 8 MB pre-flash backup |
| Flash / PSRAM | PASS: 8 MB each; QIO / OPI |
| Native USB logs | PASS after Serial.write; IDF stdout alone did not reach native USB |
| Sense SD adapter | PASS using CS21 / SCK7 / MISO8 / MOSI9; upstream SD_MMC failed |
| Card formatting | PASS: owner confirmed empty card and authorized agent format; exFAT replaced with FAT32 |
| Card metadata | PASS: new partition LBA 63, signature 55aa; capacity 127,865,454,592 bytes |
| Independent SD write/read | PASS: 262,144 bytes, full pattern match, 728 ms write; owned test file removed |
| Original recorder SD | PASS: mount, config/UI reads, real AVI write, directory listing and HTTP download |
| Camera | OV3660 PID 0x3660; 20 MHz JPEG failed decode, repeated 10 MHz captures pass |
| Actual AP connection | PASS after stale AP flag fix; fresh scan 99% signal, Windows DHCP 192.168.4.2 |
| Original root page | PASS: 103,658 bytes; upstream assets provisioned and readback-MD5 verified |
| HTTP still / stream | PASS: complete JPEG plus three MJPEG frames, all decoded with FFmpeg -xerror |
| Manual startup | PASS: second config reload disables motion; only Start Recording instruction appears |
| PDM initialization | PASS: GPIO42 CLK / GPIO41 DATA, I2S0; isolated 3 s mono 16 kHz WAV |
| Real AVI codecs | PASS: MJPEG 640×480 plus pcm_s16le, 16 kHz mono; complete decode |
| Audio sample-length regression | Reproduced: 20 s audio falsely labeled 40 s; fixed dwLength to samples rather than bytes |
| Video timing regression | Reproduced: 4.6 fps rounded to 5 made 20 s video 18.6 s; header now uses measured rational frame rate |
| Post-fix real 20 s AVI | PASS: video 20.109 s, audio 20.160 s, difference 0.051 s |
| Physical microphone input | PASS: four 880 Hz PC pulses detected; on/off tone amplitude ratio 12.45 |
| Speech / subjective noise | NOT VERIFIED; tone capture does not establish speech quality |
| Original OTA | PASS twice: app0 → app1 (0x340000), then app1 → app0 (0x10000) |
| Post-OTA boot / HTTP | PASS: running partition logged, root page accessible, camera/SD/PDM startup completes |
| PC Wi-Fi restoration | PASS after every bounded AP visit; temporary profile removed; no saved passwords read |
| 2-minute AVI | PASS: video 120.000 s / PCM 119.936 s; difference 0.064 s; complete decode |
| Final stress tests | 20 integrated WG/audio-record cycles PASS; five-minute AVI complete decode / 72 ms difference; independent 30-minute idle and whole 47.7 MB WG download PASS; actual missing-card/reinsertion PASS, full/corrupt SD pending |
| Minimal preview / audio AVI / OTA | PASS: all image/AVI decodes, four physical tones, app0→app1 and post-boot HTTP |
| Minimal 20 s timing | PASS: 20.091 s video / 20.160 s audio; 69 ms difference |
| NVS config store | PASS: 78 isolated checks, 5 real software reboots, pending/active banks, interrupted trial/commit, CRC/schema recovery and legacy migration |
| Actual failed-Wi-Fi rollback | PASS: 37 standalone checks; final 32-check product run includes recording throughout failed Wi-Fi and interrupted-trial reboot |
| Password/session/WG import module | PASS: 65 isolated hardware checks; Python PBKDF2 known answer, atomic import, Unicode policy, expiry/revocation/rate limits |
| Async HTTP authentication | PASS: 42 real HTTP checks; Cookie/CSRF, malformed requests, logout, verifier change and in-flight revocation |
| HTTP during password work | PASS: 160 isolated probes, maximum 0.138 s; password login ~35 s |
| User WG config import | PASS: actual ignored file parsed on board, 1 IPv4 route and 1 explicitly excluded IPv6 route; no NVS/network application |
| Camera auth / embedded dashboard | PASS: 67 real checks in synthetic RAM fixture; anonymous/Basic denial, CSRF, old routes absent, bounded JSON, no query controls, camera/recording during KDF |
| Existing-stream revocation | PASS: logout closes the live stream in 0.094 s; other client's session remains usable |
| Integrated HTTP responsiveness | PASS: 198 probes, maximum 0.135 s; 622 preview frames; concurrent login 45.701 s; no connection retries |
| Integrated audio AVI decode | PASS: both SD downloads decode fully as MJPEG 640×480 plus 16 kHz mono PCM; video/audio duration differences 16 ms and 8 ms |
| Restore after auth fixtures | PASS: xiao-minimal app0, camera/SD/PDM initialized; fresh still and three preview JPEGs decode; original PC Wi-Fi restored and temporary profiles removed |
| New embedded assets | PASS: board serves source-matching HTML/CSS/JS without CDN or SD assets; real Chrome interaction/layout/visual gate passes 38 checks; later actual missing-card boot and reinsertion also pass |
| Product account / setup / Wi-Fi recovery | PASS: chosen password privately enrolled, AP proof rejected/LAN proof accepted, persisted login after reset, recovery AP closes, last known good preserved |
| Administrator password rotation | PASS 85 checks: cancel on originating logout, wrong-current rejection, actual Chrome success, all prior sessions revoked, normal-reset persistence, real 80 MHz change; chosen password/all 31 values/240 MHz restored and 78 preexisting media entries preserved |
| Password-work capture and responsiveness | PASS concurrent full MJPEG/PCM decode, 8.356 s video / 8.352 s audio (4 ms difference); fifteen status samples at most 141 ms; actual changes 94.22 s at 240 MHz with recording and 203.57 s at 80 MHz |
| Final account UI / image | PASS 42 checks: authenticated final OTA/exact four assets, three languages/nine widths, real wrong-current proof/live errors, local policy/mismatch rejection, cleared inputs, one expensive request, no JS/overflow/private browser storage; selected revision 47, camera/SD/WG healthy |
| Unconfigured appliance access | PASS: 33 real checks on non-fixture build; no account created; public assets available, protected APIs locked, malformed/nested login rejected, literal escaped backslashes accepted |
| Appliance build / budget | PASS: media product 1,763,760 bytes = 52.77%, static RAM 105,524; prior setup 52.25% and auth fixture 54.63% |
| Product photos / media | PASS 44 real-account checks; paging, JPEG save/preview/download, AVI downloads, CSRF/path/recording guards, logout revocation in 0.015 s; ten originals preserved |
| Product media decode | PASS full JPEG and two MJPEG/PCM AVI decodes; photo hashes match; new video 9 ms and network-trial video 58 ms duration differences |
| Product authenticated LAN OTA | PASS 64 checks: auth/CSRF/Origin/size/chip/hash, corrupt and revoked upload retain slot, app1→app0→app1, healthy HTTP confirmation and real failed-health fallback; two final JPEGs decode |
| Independent WG compatibility | PASS patched PR #52 on same core; unmodified release/main/PR candidates failed compile |
| Independent WG sessions / data / reconnect | PASS: 20 valid sessions, 20 bidirectional HTTP 301 probes, 3 deliberate Wi-Fi reconnects, 40 LAN checks |
| Remote WG access | PASS: owner and PC VPN fetched experimental JSON at 10.8.0.5:8080/status |
| Camera-integrated WG-A | PASS final 33-check core, 289 lifecycle and 44 fault gates; user additionally verified actual remote production login/photo/video at 10.8.0.5 |
| WG-A Chrome UI | PASS 34 checks: real file/field import, local confirmation, restart/clear/cancel, masked/cleared keys, 390/768/1366 px layouts, no JS exception/storage |
| WG-A firmware budget | PASS: final 1,821,248 bytes = 54.49%; static RAM 112,156; explicit route hook, MTU and usable-session status |
| Authenticated WG OTA | PASS actual remote installation, boot and chosen-account re-login; refinement image 1,881,856 bytes and wake-fix 1,882,000 bytes |
| Capture refinements | PASS 131 real-board checks: 31 parameters, eight actual photo resolutions through 2048×1536, safe UTF-8 photo/video renaming with unchanged bytes, 80/160/240 MHz audio AVI, persisted values and three standby wakes |
| Refinement UI | PASS 46 real Chrome checks through WG: Chinese/English/German, nine responsive layouts, zero JS errors, language-only storage, actual parameter save/restore, rename and 3 MP preview |
| Final refinement image | PASS WG OTA/re-login and preserved parameters; 1,882,144 bytes = 56.31%, static RAM 112,476; additional 44 focused checks pass |
| Actual microphone stop/start | PASS 37 checks on WG-updated 1,882,608-byte image (56.33%, static RAM 112,476); RX handle telemetry proves off/idle stops input and reenable/wake restarts it; still/photo remain available with mic off; two complete audio AVI decodes differ 3/19 ms; original values/media preserved |
| Microphone refinement UI | PASS 50 real Chrome checks: all prior layout/language/capture checks plus clock-aware login hint and three mic-off scope labels; zero JS errors, language-only storage |
| Lowest-clock authentication diagnostic | PASS 8 checks at actual 80 MHz, login 168.09 s; WG/status/JPEG available and original clock/settings restored. Former 120 s client deadline failed and the 150 s UI wait was too short; revised-page gate is separate |
| Active TCP during WG teardown | PASS 32 fixed-fixture checks: three real bound-TCP restarts, no panic/reboot during teardown, new WG sessions/data, unchanged configuration and working camera/SD/JPEG; original-order fixture reproduced null-state panic and activated the crash guard, recovered through authenticated OTA without guard/RTC changes |
| Actual lowest-clock Chrome login/wake | PASS 27 normal-image LAN checks at 80 MHz after real camera/PDM standby: 101.63 s login, 31 fields, actual 640×480 still wake and selected clock restored, no JS/overflow, language-only storage/logout and original values/240 MHz restored; password-work status transactions at most 156 ms |
| Earlier normal recovery image | PASS authenticated LAN OTA and source-matching assets/values: 1,882,672 bytes (56.33%), static RAM 112,476, SHA-256 86aff6a7dcdb2a189b0b15992a7432ac40dc69ed84d2f8922fc672f9ff36e00e; WG teardown fixture no longer deployed |
| Mixed-clock deinit standby regression | FAILED repeatedly at 160 MHz after 80 MHz audio recording; a single 58-check audio-first candidate pass was followed by the same failure in the 34-check production run. Archived independently; authenticated restart restored camera/all values/240 MHz without WG panic |
| OV3660 sensor-power-down standby | PASS 102/101-check runs: 80/160/240 MHz, 18 real standby wakes, checked power-down readback and fresh frame-start timestamps, six fully decoded audio AVIs (3–29 ms differences), no reset, original media/values/240 MHz restored; photo and page gates separate |
| Saved 3 MP photo from sensor standby | PASS 44 checks: actual 2048×1536 JPEG saved/downloaded/fully decoded at each of 80/160/240 MHz after real sensor/PDM standby; checked fresh wake frame, SD/WG/no reset and preservation of original media/values/240 MHz |
| Sensor-standby lowest-clock mobile UI | PASS 25 actual Chrome checks: 101.53-second 80 MHz login, actual sensor/PDM standby and checked fresh still wake, 31 controls, visible status/buttons update, zero JS/overflow and language-only storage, original values/240 MHz restored; three password-work status transactions at most 177 ms |
| Audio gain | PASS controlled physical 880 Hz: 4× setting gives 4.62× measured amplitude relative to unity, no clipped samples; subjective speech/noise remains unverified |
| Idle audio / high-resolution video | PASS direct standby-to-audio recording, HD 1280×720 and UXGA 1600×1200; complete decode, differences 18/32/61 ms; HD/UXGA actual frame rates around 9/7 FPS |
| Minimum free space | PASS safe quota simulation: photo/start return 507, preexisting media unchanged and original settings restored; card was not filled |
| Thirty-minute idle | PASS 17 checks, 1,800.195 seconds and 169 observations; no reset, WG session observed throughout, stable standby heap 159,640–159,644 bytes / PSRAM minimum 5,717,840; status maximum 1.367 s; fresh data probe and JPEG wake pass |
| Large WG download | PASS 26 checks, 47,667,154 bytes in 533.55 s; whole-file reference SHA-256 match and full MJPEG/PCM decode, no reset and unchanged network revision |
| Missing-mount recovery | PASS 24 real-board checks with explicit skip-SD fixture: embedded UI/auth/WG/JPEG and restart remain usable, SD APIs return 503, normal WG OTA restores all media paths/sizes and capture values; physical removal not tested |
| Earlier physical-card baseline | PASS 11 checks on the sensor-standby image, 78 media entries/31 values/revision 45; retained as historical evidence, not the baseline for the newer account image |
| Final account-image physical-card baseline | PASS 11 checks: normal SD/camera/WG, exact four assets/JPEG full decode, 79 media entries/31 values/revision 47/CPU 240; actual absent/reinserted stages recorded separately |
| Real physical missing-card / unused frame-analysis regression | Original functional checks pass but serial replay FAILS (814 light-analysis errors, zero colorDepth without optional SD config). Fixed manual image skips unused motion/light/PIR work; actual no-card OTA and repeated 24-check gate/180-second serial replay PASS, zero such errors; normal restart/login/WG/JPEG/explicit SD refusal and revision/values preserved |
| Real power-off SD reinsertion / new writes | PASS 28 checks: all 79 prior media paths/sizes, 31 values and revision 47 preserved; new photo and real PDM AVI saved, whole downloaded and fully decoded; AVI 1,075,658 bytes, video/PCM 8.277/8.288 s (11 ms); 120-second serial capture has no old frame-analysis errors |
| Baseline compile regression | PASS xiao-baseline: 1,786,048 bytes = 53.44%, static RAM 99,208; fixture 1,855,136 bytes = 55.5%; neither replaces the deployed normal appliance |
| WG-B isolated full-route/output guard spike | PASS 39 checks: ordinary CA-verified HTTPS exits at server and increments WG payload by 1,145/3,674 bytes; 18 output packet cases, all DNS slots, 45-second tunnel absence with physical block/LAN JPEG, reconnect, complete PCM AVI decode (6 ms), normal OTA restore and 81 prior media preserved. Product persisted/bootstrap/lifetime gates remain incomplete |
| Current-source normal image after spike | PASS build and authenticated OTA: 1,878,160 bytes = 56.19%, static RAM 102,636; original login/assets/31 values/revision 47/actual CPU 240/camera/SD/WG, fixture route 404. Archived as appliance-full-spike-normal.bin; fixture serial 400.2 s has no panic/watchdog/stack errors |
| Production WG-B mode / lifecycle | PASS 35 normal-image checks: mode-only authenticated LAN trial, strict schema/CSRF, real expected public exit/WG data, cancel/confirm, persisted full-mode reset, peer restart proof revocation/recovery, cancelled remote trial returning to full proof; capture values and actual 240 MHz retained |
| Persisted full-policy / real faults | PASS 50 fixture checks, including 75 actual physical-output packet cases with zero failures; clock-zero cold boot/NTP, bounded exact hostname DNS, no secondary DHCP resolver, absent WG/public block/LAN JPEG, wrong RAM endpoint recovery, actual Wi-Fi disconnect/DHCP return, cancelled and reset-interrupted trials, all 82 original media and 31 values retained; normal firmware restored |
| Public-probe TLS lifetime regression | RED: four admitted probes lose 93,952 heap bytes and last two fail HTTP; GREEN: all four return HTTP 200 and real full proof, 144,780 free heap before/after every request, zero retained loss. TLS/client scopes end before task deletion and HTTP reuse is disabled |
| Full-policy serial | PASS: 600.2 seconds / 13,594 bytes, no panic/watchdog/stack overflow or legacy frame-analysis errors; injected packet cases are intercepted by an output spy, not emitted on the radio |
| Final WG-B UI / normal image | PASS 42 actual Chrome/media checks: exact assets, no fixture routes, three languages/nine widths, actual locally confirmed mode, live verified exit, no overflow/JS/private storage, whole new photo and PDM AVI decode; all 82 earlier media and 31 values retained, actual CPU 240 and selected full-mode revision 71 |
| Final full-mode PCM AVI | PASS: 1,461,338 bytes, MJPEG 640x480 / 16 kHz mono PCM, video 8.133 s / audio 8.128 s (5 ms); saved photo and fresh JPEG also fully decode |
| Current normal WG-B budget | PASS: appliance-full-ui-final.bin, 1,894,496 bytes = 56.68%, static RAM 103,188; authenticated LAN OTA, no fixture endpoints and six-value BIN/ELF audit |
| Final automatic public-proof refresh | PASS: 190.61 seconds / 59 observations, four actual automatic proofs without forced restart/mode change; guard/expected exit/camera/SD/revision/uptime healthy; completed-probe heap 140,376–144,428, final gain 4,052 bytes (no retained loss) |
| Final normal serial | PASS: 600.2 seconds / 4,126 bytes, no panic/watchdog/stack overflow or legacy frame-analysis errors |
| Final full-mode remote inbound | PENDING owner confirmation from another existing WG peer; earlier WG-A remote login/photo/video is preserved separately |
| Full-tunnel acceptance | PENDING: DNS/leak/default/exception policy and public exit proof |

## Artifacts and reproducibility

Latest product evidence is in ignored `appliance-refinements-wake-fixed/report.json`
(131 checks), `browser-refinements/report.json` (46), `refinement-edges/report.json`
(44), and `appliance-refinements-final-install.json`. That image SHA-256:
`4d22692cd367718542dbbbf2a70b955769a7f76740bc99a3aba8a082eea09a80`.
The first standby failure was reproduced, corrected by complete camera-config
initialization and bounded wake waiting, then retested. The browser's first
German reload failure was an `innerText` read in a closed `details`; actual saved
language and text were correct. Failed reports remain archived.

The newer microphone stop/start image is archived as `appliance-microphone-final.bin`,
SHA-256 `6145c185c173f78e636393c1b24a7f1a2009100d5aad60137c3bb112c3c0f300`.
It was installed by WG OTA and passes `microphone-power/report.json` (37 checks).
Separate long-idle and large-transfer reports are `appliance-stability-final-wg/report.json`
and `appliance-large-wg/report.json` on the prior final refinement image.
The login-deadline image is 1,882,624 bytes, static RAM 112,476, SHA-256
`3dfb3fe2432becb99302b6edba21be0d5d03e303b129d1d567bd5a2489b47d07`.
Authenticated WG installation preserves capture values; all four deployed UI
resources match source. Low-clock browser reports remain separate, including
the CSP-blocked automation wait failure before private input was entered.

The sensor-power-down normal image is archived as
`appliance-sensor-standby-final.bin`/`.elf`/digest: 1,883,568 bytes (56.35%),
static RAM 112,476, SHA-256
`eb1b07569ce449a97f3a2cd0275340fa5f48bb2ee846c284104c5bf704436c42`.
Its independent `low-clock-sensor-standby`, `low-clock-sensor-standby-repeat`
and `sensor-standby-photo` reports pass 102/101/44 checks. The first run includes
authenticated LAN OTA. Temporary debug probes and the WG teardown fixture are
absent. Idle camera buffers now remain allocated: short photo-gate standby
free PSRAM is 2,869,124–2,871,968 bytes, free internal heap 135,792–135,840.
This is not the prior deinit-mode 30-minute measurement. Failed deinit-mode
reports `low-clock-wake`, `low-clock-wake-diagnostic` and
`low-clock-wake-power-final` remain archived rather than overwritten.
The same image's 25-check actual mobile page report and visually inspected
viewport are in `low-clock-browser-sensor-final`; no extra OTA is used to make
that gate pass. Source-matching embedded assets are checked independently by
the normal physical-card baseline.

Private flash backups, room imagery, audio, HTTP snapshots and logs remain under ignored `.tools/hardware/`.

The final administrator-settings image is archived as `appliance-account-final.bin`,
`.elf` and companion digest: **1,896,416 bytes (56.74%)**, static RAM **113,804**,
SHA-256 `2bb82eda8ad0eb63caffcb76d5db3144df3557e9a5ea42575a6e62c29c410796`.
The 85-check `account-candidate` gate uses the identical auth/config backend,
1,896,480 bytes, SHA-256
`06ac7eea167e86c407d6f38ef54ec49811d7783a865bfc6a5ea9de25be9fe996`.
Only the UI's unconfirmed-result wording changes before the 42-check final gate.
The 1,095,026-byte concurrent clip fully decodes with a 4 ms duration difference.
Twenty minutes of serial capture contains no panic/watchdog/stack error; planned
OTA/normal-restart events are retained. The chosen password is restored; the
temporary recovery secret is retained only in the ignored private directory and
included in the six-value private audit. The new physical baseline is in
`physical-card-account-final/baseline.json`, not the older revision-45 directory.

The subsequent physical missing-card capture fix is archived as
`appliance-manual-frame-final.bin`/`.elf`/digest: **1,878,176 bytes (56.19%)**,
static RAM **102,636**, SHA-256
`b1f910b5d3917b44f659f99a7e13780f8d62199ca59f207f186fb9e5ecb81fe2`.
Compared with the account image, unused frame-analysis elimination removes
18,240 binary and 11,168 static RAM bytes. Auth/config/UI code is unchanged.
Evidence: `physical-card-boot-fix/installation.json`, `absent-report.json`,
`absent-serial.log`, `motion-defaults-green.json`, `restored-report.json`,
`restored-serial.log` and whole downloaded media; original red log/replay are
retained in `physical-card-account-final/`. Baseline compilation still passes:
1,786,048 bytes, static RAM 99,208; it is not installed on the board.

- `before-baseline-flash.bin`: 8,388,608 bytes; SHA-256 `4f3b36fa803a07d56dcb11341b629f2ab932066383268e0fe4830d8a31de2cd0`.
- `sd-format-fat32.log`, `fat32-diagnostics/diagnostics.log`: explicit format command, FAT32 metadata, complete SD readback and independent captures. The first formatter log inspected the old LBA 2048; the subsequent independent check verifies the new LBA 63.
- `diagnostics/`: transferred 20 MHz JPEG fails Huffman decoding; `diagnostics-10mhz/`, repeat and post-format captures decode successfully.
- `seed-assets.log`: common.js 53,112 bytes / MD5 `4c6df4490ff6c3f68cd6371506118e02`; MJPEG2SD.htm 103,658 bytes / MD5 `f447b0244d97b6864784e9e51d715042`.
- `ap-fixed-boot.log`, `http/probe-report.json`: controlled restart, manual config reload, successful root/still/stream.
- `http/baseline-recording-before-audio-header-fix.avi`: original 20 s audio header regression. A header-only hypothesis copy passed before changing firmware.
- `http/baseline-recording-before-video-clock-fix.avi`: low-frame-rate rounding regression; audio 20.128 s versus video 18.6 s.
- `ota-audio-header-fix.log`, `http/ota-app1-report.json`: first OTA and actual app1 boot.
- `ota-video-clock-and-tone-recording.log`, `http/ota-report.json`: second OTA and app0 boot.
- `http/baseline-tone-recording.avi`, `tone-record-report.json`, `tone-avi-validation.json`, `tone-validation.json`: real post-fix recording, codec/timing validation and physical tone detection. AVI SHA-256 `774bcc812acc5b94813ed9a1de7185223774b8216ba9da37b6acc97cb2086057`.
- `two-minute-recording.log`, `http/baseline-recording.avi`, `record-report.json`, `two-minute-avi-validation.json`: 120 s capture, 8,986,062-byte download; SHA-256 `e31c700512e51f53ac3648eb6c202bccd715ab24e11968052e7b2065c8830ae7`.

Current audio application SHA-256: `e450dd386ac2a4ac8163171994e84e3a0b2f193d52f01c3d0f32db4fdfcd96b8`.

| Build measurement | App | Bootloader | Partitions | Factory |
| --- | ---: | ---: | ---: | ---: |
| Original host baseline | 1,808,720 | 19,968 | 3,072 | 1,874,256 |
| Earlier no-audio SPI/USB profile | 1,785,680 | 19,968 | 3,072 | 1,851,216 |
| Current audio / AP / AVI fixes | 1,823,584 | 19,968 | 3,072 | 1,889,120 |
| Verified minimal audio profile | 1,821,360 | 19,968 | 3,072 | 1,886,896 |
| Current restored minimal profile | 1,821,376 | 19,968 | 3,072 | 1,886,912 |
| Embedded camera/auth development | 1,830,560 | 19,968 | 3,072 | 1,896,096 |
| Camera/auth RAM fixture | 1,825,856 | 19,968 | 3,072 | 1,891,392 |

Normal uploads preserve NVS at 0x9000 and use hash-verified discrete regions. No full-chip erase, factory-image upload or eFuse changes. The one-use formatter is separate from daily firmware; daily automatic formatting and deletion remain disabled.

Intentional USB resets before setup completed left upstream's RTC crash-loop latch set. One verified four-byte RTC RAM word was cleared for recovery; the production guard remains intact. HTTP controlled restart clears the latch through the upstream API.

`nobuild upload` failed before any flash write because image/address arguments were missing; use normal build/upload. Early managed-session Core restrictions are historical.

```powershell
python -m unittest discover -s tests -v
python scripts/validate_avi.py .tools/hardware/http/baseline-tone-recording.avi
& 'C:\Program Files\ffmpeg\bin\ffmpeg.exe' -v error -xerror -i .tools/hardware/http/baseline-tone-recording.avi -f null NUL
python experiments/hardware/analyze_tones.py .tools/hardware/http/tone-capture.wav
```

Header semantics follow [Microsoft's AVISTREAMHEADER definition](https://learn.microsoft.com/en-us/previous-versions/ms779638(v=vs.85)): dwLength uses the dwRate/dwScale time units. The changes preserve the upstream mux and recording flow.

Additional ignored evidence: `minimal-http-restart-and-record.log`,
`minimal-ota.log`, `http/minimal-*-report.json`, `minimal-tone-recording.avi`,
`minimal-avi-validation.json`, `minimal-tone-validation.json`,
`config-nvs-test.log`, `config-nvs-report.json`, `wg-twenty-cycles.log`,
`wg-cycle-report.json` and `wg-remote-status.json`.
Minimal AVI SHA-256: `f7b1162bb425d6ac4074a3580a20216f855b33395c6c13dcd650770b3cde207e`.
Earlier minimal app SHA-256: `b1f08d06b942d0aace2f1277b32fb551ba28cd39e14e56f9625f7add0e88169c`.
Current restored minimal app SHA-256: `b14e0274a93b72bd0e404beb611b80757c2f9d6d6aa01fb93bbd4f5738066c00`.
The tone analyzer now chooses a four-pulse match before comparing contrast;
removing one measured pulse in a negative fixture correctly fails the check.

The hardware baseline is a local development milestone. Authenticated LAN OTA
has its separate 64-check gate; OTA over WG, missing-SD recovery, integrated WG and final
stress acceptance are not implied. No remote publication is claimed.
See [NVS evidence](CONFIG_STORAGE.md) and [WG evidence](WIREGUARD_BACKEND.md).

Authentication/import evidence: `config-auth-test.log`, `config-auth-report.json`,
`auth-http-boot.log`, `auth-http/report.json` and `wg-private-import-report.json`.
The 65-check suite used synthetic input and no NVS/SD writes. Hardware SHA KDF
work on a CPU1 priority-one worker takes 34,099 ms; observed HTTP login polling
completes in 35.15–35.47 s. No watchdog configuration was changed. A priority-zero
run took 68,426 ms; a portable-software probe took 97,911 ms and was discarded.
These isolated measurements do not establish final camera/WG workload latency.
See [authentication contract](AUTHENTICATION.md).

Camera auth evidence: `appliance-auth/report.json`, `appliance-auth-boot.log`,
`appliance-auth/still.jpg`, `appliance-auth/preview-*.jpg` (all four JPEGs decode),
plus the independent downloaded AVI noted below. The fixture account remains
in RAM and never opens/writes `camcfg`; no real admin password was used.
The requested 15 FPS recording reports 283 frames / measured 13.9 FPS and
326,144 real PCM samples while password work and live preview run concurrently.
No panic/watchdog/stack error follows its authenticated recovery restart.
See [HTTP integration boundary](APPLIANCE_HTTP.md).

The separate network manager's real underlay gate passes 37 checks (30 board,
7 HTTP/host). It applies privately supplied working Wi-Fi only in owned
`nettrial*` namespaces, rejects activation from an AP socket, confirms over the
actual LAN, restores the prior selected configuration after the default
60-second failed-SSID attempt, reboots during another marked trial, recovers
without retrying it, and reconnects after deliberate Wi-Fi loss. Cleanup and
PC Wi-Fi restoration pass. Later root camera integration and WG trial gates
also pass as listed above. See [NETWORK_TRIAL.md](NETWORK_TRIAL.md) and ignored
`network-trial-report.json`, `network-trial-firmware.bin`/`.elf`.

Both bounded camera-auth recordings were downloaded using `baseline_http.py
collect --sd-path` after restoring the minimal recorder. Independent ffprobe
and complete FFmpeg `-xerror` decode pass:

| Recording | Bytes | Video seconds | PCM seconds | Difference |
| --- | ---: | ---: | ---: | ---: |
| First integration visit | 2,531,806 | 20.592 | 20.576 | 16 ms |
| Final 67-check visit | 2,538,718 | 20.376 | 20.384 | 8 ms |

Ignored files: `appliance-auth-first-recording.avi`,
`appliance-auth-final-recording.avi`, their `*-collect-report.json` and
`*-avi-validation.json`. SHA-256 respectively
`8cf4e4240f04c5707c4f0e6ee36e30d8f82b1881bf457fdcc3adeda08534cccf` and
`6791c62696ec326c6988d1f960d8efd7bd8acd383748238604145522212c332f`.
These duration checks do not establish subjective speech quality or long-term
A/V drift.

The first immediate post-restart baseline stream probe hit its five-second
read timeout; the board logged three frames and normal startup, without a
panic/reset. A fresh bounded visit after startup passes without changing the
timeout or firmware, and all four restored JPEGs decode. Both reports are
preserved as `appliance-baseline-first-probe-report.json` and
`appliance-final-baseline-probe-report.json`; boot evidence is in
`appliance-final-baseline-restore.log`.
