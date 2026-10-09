# Isolated XIAO hardware utilities

Clock gate (normal firmware, no fixture): `test_appliance_clock.py --address
192.168.1.181 --video --out .tools/hardware/appliance-clock/green.json`
checks the actual NTP UTC clock, Berlin local clock, a saved photo and a short
audio/video recording. `--firmware <normal.bin>` installs a supplied normal image;
`--reboot` separately checks cold boot and legacy-config reload. It refuses active
capture modes, removes only its owned media, and restores the conversion switch.
The POSIX winter/summer/transition/midnight gate uses the Windows WSL runtime:
`wsl -d Ubuntu-24.04 --exec python3 /mnt/c/Users/Jingjie/Desktop/ESP32/tests/test_clock_timezone.py`.

BLE / scheduled-photo / MP4 gates:

- Build `xiao-appliance` and `xiao-appliance-codec-test` separately before running
  `run_feature_acceptance.py`. The driver uses the one board sequentially; do
  not run another device script at the same time.
- `appliance_codec_probe.py --reuse --keep-fixture --width 320 --fps 5` records
  an owned clip, checks every source JPEG's dimensions, performs on-board
  conversion and full FFmpeg audio/video decode, and compares exact frame rate,
  frame count, duration and SSIM. Use `--width 640 --fps 10 --seconds 60` for
  the longer gate. `--require-wg` additionally requires a successful public-route
  proof refresh during actual conversion. It removes only its owned SD files and restores settings.
- `test_appliance_features.py` exercises real scheduled captures, 20-item
  pagination, busy/mutual-exclusion guards, metadata, reboot and Range requests.
  `test_timelapse_index.py` separately verifies progress beyond 4096 missing
  intents in an owned folder.
- `test_conversion_queue.py --keep-mp4` exercises actual publish/delete,
  larger-output acceptance, journal recovery, corruption refusal, foreground
  abort/restart and interrupted-part cleanup. `test_mp4_browser.py` uses the
  resulting owned manifest for native Chrome playback/seek and then deletes
  that MP4. Use the isolated browser Python for browser scripts.
  `prepare_mp4_browser_fixture.py` creates a new owned MP4 if a browser-only
  harness retry needs fresh media; it does not reuse or delete historical clips.
- `test_feature_browser.py` tests CN/EN/DE layouts, real folder controls and PIN
  response/cache clearing on logout. `appliance_ble_checks.py --wifi` reads the
  ignored `.tools/private/wifi-password.txt` and adds actual Wi-Fi success/rollback to secure
  pairing, notification, reboot/reconnect and owned photo-session tests.
  `test_ble_usb_pin.py` checks explicit USB PIN recovery without saving its value.
- `soak_timelapse.py` defaults to the six-hour gate. Shorter runs must be labelled
  with their actual duration and cannot establish six-hour reliability.
  Turn history conversion off first to freeze the media baseline. It checks
  actual camera/SD/Wi-Fi/BLE health, schedule/file accounting and old media,
  then removes only its owned Session. Public-route proof and memory samples
  are evidence to inspect separately. Safe passive UART observation may run
  alongside it, using `serial_markers.open_console()` without changing DTR/RTS.
- `test_features_release.py` installs the archived normal image and checks its
  real camera, original capture settings, source-matching assets, unavailable
  fixture routes and public-route proof. `--finish` keeps that image installed,
  saves conversion enabled, reboots, and verifies the setting and camera again.

The codec fixture allowlists only explicitly submitted AVI paths and never
processes historical footage automatically. Its codec/index fault-injection
routes are excluded from the normal firmware. Install the normal image after
testing. Never erase NVS or clear the SD card to make a gate pass. PINs,
credentials and cookies stay in RAM; private reports/media remain ignored.

New event/media gates:

- `test_appliance_events.py --address ... --firmware <event-test.bin> --restore
  <normal.bin>` uses the explicit RAM scene fixture after real JPEG decode,
  records actual VGA/PDM AVI files, checks quiet-tail continuation, brief events,
  independent names, synchronization, settings restoration and normal OTA cleanup.
  It preserves existing media and does not claim physical motion sensitivity.
- `test_appliance_event_media_browser.py --address ... [--firmware <normal.bin>]`
  creates owned photos/video, tests batch auth/schema/busy validation and the
  actual three-language UI, checkbox selection and delete-all. Browser media
  inventories are scoped to owned test files; every real deletion passes an
  ownership guard. A separate intercepted 41-file / 21-date scan exercises both
  pagination cursors without touching user files. Secrets/cookies remain in RAM.

Never ship `xiao-appliance-event-test`; the final normal firmware has no scene
substitution endpoint. Reports/screenshots/recordings remain in `.tools/hardware`.

Temporary firmware, separate from the recorder. Restore the root audio recorder after testing. All captures/media live in ignored .tools/hardware.

- `diagnostics`: no formatting or deletion of existing files. A successful SD mount triggers a unique 256 KB test-file write/read/delete; JPEG and three-second PDM WAV use length/MD5 USB framing. Raw metadata identifies filesystem type.
- `format-empty-sd`: destructive one-use utility, authorized by the owner on 2026-10-03 after confirming the inserted card was empty. Requires the known board MAC, the observed exFAT partition and the exact USB FORMAT_SD_FAT32 command. Refuses a FAT32/already-formatted or different card. Uses FM_FAT32; may create a fresh partition boundary. Never included in recorder firmware.
- `seed-assets`: embeds only repository public common.js/MJPEG2SD.htm, creates them only if absent and verifies readback MD5. No config/credential embedding or existing-file overwrite.
- `baseline_http.py`: bounded temporary AP visits for upstream HTTP checks, with original Windows Wi-Fi profile restored and helper profile removed in finally. No saved passwords are read.
- `scan_wifi.py`: fresh Windows scan using [WlanScan](https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlanscan); no profile or credential changes.
- `analyze_tones.py`: measures the four quiet 880 Hz PC stimulus pulses in captured PCM; speech quality remains separate.
- `config-nvs`: versioned active/pending storage tests across five software
  reboots, with selector/CRC/schema fault injection. Synthetic credentials,
  known-board check and USB start; clears only owned test namespaces.
  Actual failed-network rollback still needs product integration. See
  [configuration evidence](../../docs/CONFIG_STORAGE.md).
- `config-auth`: 65 synthetic import/password/session checks, including Python
  PBKDF2 known answer, Unicode policy, CSRF, expiry and rate limits. Explicit USB
  start and known board; no NVS or SD writes.
- `auth-http`: isolated AP `XIAO-Auth-Test-084C` with a synthetic verifier and
  asynchronous login. `test_auth_http.py` passes 42 real HTTP checks and restores
  the PC profile. No camera/media/product config or real admin password.
- `test_private_import.py`: USB validation of an ignored local `.conf`; records
  only a sanitized summary, without applying network/NVS. See
  [authentication evidence](../../docs/AUTHENTICATION.md).
- `test_appliance_auth.py`: 67 real camera/auth checks against the root explicit
  RAM fixture `xiao-appliance-auth-test`, including concurrent recording/preview
  during password work and revocation of an existing stream. Restores the PC
  Wi-Fi profile, saves no credentials/tokens, and leaves one bounded AVI for
  independent validation after restoring the minimal recorder. See
  [root integration](../../docs/APPLIANCE_HTTP.md).
- `baseline_http.py collect`: saves a read-only AVI inventory, downloads exactly
  one new AVI compared with `--previous-inventory`, or fetches an existing
  upstream AVI selected by `--sd-path`; never deletes media.
- `network-trial` / `test_network_trial.py`: 37 real Wi-Fi/NVS/HTTP checks using
  USB-supplied credentials and a temporary random test token. Only owned
  `nettrial*` namespaces are written and removed. Tests actual LAN confirmation,
  AP rejection, failed-network rollback, trial reset and runtime reconnect;
  restores the original PC Wi-Fi. Not yet integrated with camera/setup/WG.

From the repository root:

```powershell
python scripts/pio.py run -d experiments/hardware -e diagnostics -t upload --upload-port COM3
.tools/platformio/penv/Scripts/python.exe -X utf8 scripts/serial_capture.py --port COM3 --seconds 60 --out .tools/hardware/diagnostics-capture.bin --reset
python scripts/extract_hardware_capture.py .tools/hardware/diagnostics-capture.bin --out-dir .tools/hardware/diagnostics
python scripts/pio.py run -e xiao-audio-baseline -t upload --upload-port COM3
```

Do not use the formatter casually or treat independent captures as original AVI/preview/OTA verification. Current results are in docs/TEST_REPORT.md.

Product regression scripts use the chosen account only in RAM and retain
existing media. `test_appliance_low_clock_browser.py` verifies real Chrome
authentication and still wake at actual 80 MHz, with a separate recovery
session and optional authenticated OTA. `test_appliance_low_clock_wake.py`
exercises real standby-to-still/settings/audio recording, accepts
`--clocks 80 160 240`, and fully decodes the owned clips. Its
`--sensor-standby` gate additionally requires checked OV3660 power-down state
and a frame-start timestamp after wake. `test_appliance_wg_teardown.py` uses the explicit
root WG teardown fixture and actual lwIP bound-TCP reset path; the red and fixed
reports must be kept separately, and normal firmware must be restored after it.
`test_appliance_physical_card.py` uses only normal firmware and requires the
user to power off before physical removal/reinsertion. See
[SD recovery](../../docs/STORAGE_RECOVERY.md) for its three stages.

Production WG-B gates use the existing ignored account/WG files only in RAM:

- `test_appliance_full_mode.py`: authenticated normal OTA, strict mode API,
  actual unbound public proof/expected server exit, confirmation/cancellation,
  persisted reset and peer restart. Starts and ends in confirmed remote mode.
- `test_appliance_full_policy.py`: the explicit `xiao-appliance-full-policy-test`
  fixture, cold clock/NTP, 75 actual output callback packet cases, absent WG,
  RAM endpoint fault, actual hostname A lookup, Wi-Fi/DHCP reconnection and
  interrupted mode trial. Use `--reset-before` to clear allocations from an old
  candidate. Its `finally` restores the supplied normal image in remote mode;
  read the report and require `passed: true`, including cleanup.
- `check_full_probe_memory.py`: four admitted real public probes with heap
  samples; temporarily confirms full mode so owner restarts are actually
  accepted, then restores confirmed remote mode. This is a diagnostic:
  additionally assert `all_probes_verified` and retained heap loss <=4096.
- `test_appliance_full_browser.py`: normal assets/no fixture APIs, three
  languages/nine widths, actual browser mode confirmation, public proof,
  saved photo and PDM AVI with whole readback/decode. Leaves confirmed full
  mode active. Keeps all prior media plus its new photo and video.
- `test_appliance_full_refresh.py`: read-only 190-second observation of an
  already confirmed full mode; requires at least two actual automatic proof
  refreshes, stable device/revision and <=4096 retained heap loss. No peer
  restart or setting change is used to trigger those refreshes.

Run mutation gates sequentially; do not run another network trial/OTA while one
is active. Use distinct evidence directories for failed and corrected runs.

```powershell
python scripts/pio.py run -e xiao-appliance-full-policy-test
.tools/platformio/penv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_full_policy.py --address 192.168.1.181 --fixture .tools/hardware/appliance-full-policy-raii-fixture.bin --restore .tools/hardware/appliance-full-ui-final.bin --reset-before --out .tools/hardware/full-policy-repeat
python scripts/pio.py run -e xiao-appliance
.tools/browser/venv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_full_browser.py --address 192.168.1.181 --firmware .tools/hardware/appliance-full-ui-final.bin --out .tools/hardware/full-browser-repeat
.tools/platformio/penv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_full_refresh.py --address 192.168.1.181
```

Archive the exact BIN/ELF/digest after each build before building a different
environment; these named archives are examples from the retained acceptance
run. Neither fixture image may be the final installed image. Detailed results
and untested external faults: [WG-B](../../docs/WIREGUARD_FULL_TUNNEL.md).
