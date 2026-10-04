# Authenticated dual-slot OTA

Updated 2026-10-03. The final receiver passes 64 real-account hardware checks,
including two slot transitions and actual failed-boot-health rollback.

`GET /api/ota` requires a session and reports only progress/error, running and
target slot, maximum image size and boot rollback capability. `POST /api/ota`
requires session, CSRF and same browser Origin, no query, exact
`application/octet-stream`, a known Content-Length and a lowercase 64-hex
`X-Firmware-SHA256`. Images must fit 80% of the next application partition
(2,673,868 bytes). The original generic upload/update routes remain absent.

The receiver validates the ESP32-S3 image header and app descriptor before
camera shutdown or any flash write. Recording/finalization, media transfer and
queued network trials and capture reconfiguration/wake prevent OTA. One async worker owns the request, session
token and 4 KiB internal-RAM buffer. Other HTTP status and logout requests remain
available; ordinary mutations and new media downloads are denied during OTA.
Existing preview loops and capture changes finish before the
unchanged upstream `OTAprereq()` stops camera/audio/timer workers. Network and
authentication owners remain alive. Watchdog settings are unchanged.

Session validity is checked throughout a bounded five-minute upload. The
pinned Arduino updater computes the entire SHA-256, verifies the image and
changes the boot partition only on successful `Update.end(false)`. Incomplete,
revoked or corrupt transfers are aborted; after camera shutdown, a controlled
restart restores the current image. No SD upload staging, filesystem/partition
update, arbitrary URL update or secret configuration upload is accepted.

The build emits `firmware.bin.sha256` alongside `firmware.bin`. The offline
page accepts that small companion file or its hash and shows upload progress,
verification/reboot and errors. The hash detects transfer corruption; it is
not a firmware signature. Upload only the project's trusted application image.
This change does not enable secure boot, flash encryption or eFuse changes.

The final profile overrides Arduino's early verification hook. A pending image
gets up to twenty seconds for PSRAM, sufficient heap and an actual loopback
HTTP bootstrap response. After at least ten seconds, it marks the image valid;
otherwise the SDK selects the previous valid image and reboots. The health test
checks the management/recovery plane, so a removable SD fault does not itself
cause firmware rollback. A crash/reset before confirmation also retains the
SDK's pending-image rollback behavior. This is separate from the recorder's
preserved RTC crash-loop guard and from configuration trial rollback.

`xiao-appliance-ota-rollback-test` is an explicit known-board fixture that
rejects its boot-health check. It is not release firmware, never erases NVS or
SD, and exists only to verify SDK fallback by authenticated OTA. Keep an
archived production binary before building another environment.

```powershell
python scripts/pio.py run -e xiao-appliance
# Archive firmware.bin/.elf before changing the build environment.
python scripts/pio.py run -e xiao-appliance-ota-rollback-test
.tools/platformio/penv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_ota.py --address 192.168.1.181 --firmware .tools/hardware/appliance-ota-firmware.bin --rollback-firmware .pio/build/xiao-appliance-ota-rollback-test/firmware.bin --install-first
```

The first 58-check gate rejects anonymous, CSRF, cross-origin, wrong-chip,
invalid-header and corrupt full uploads. Logout revokes an existing paused
upload; status responds in 0.065 s, and camera/SD recover in the same slot.
Two valid updates switch app0 to app1 to app0; all post-boot camera/SD/PDM checks
pass and the JPEGs are saved for complete decoder validation. Evidence is
ignored in `.tools/hardware/appliance-ota/initial-without-boot-health-report.json`
and `appliance-ota-boot.log`.

The final 64-check gate adds a metadata-only oversized upload rejection,
authenticated installation of the final receiver, and an actual boot-health
rejection image. It switches app1 to app0 to app1, installs the explicit fault
fixture, and returns to the previous valid app1 with the original configuration,
account, camera and SD. The receiver remains responsive during a paused upload
(status 0.072 s); revocation prevents slot activation. Both final post-OTA JPEGs
decode completely. No panic/watchdog/stack corruption marker is present in its
boot log. Reports: `appliance-ota/report.json`, `decode-report.json` and
`appliance-ota-health-boot.log`, all ignored. The fixture is no longer running.
Verified production binary: 1,778,768 bytes (53.22% of a 3,342,336-byte slot),
static RAM 105,684 bytes, SHA-256
`b7f8e66892d55cd1d6ece6186d84e6181a34e8841c2ce0746e8bf4443d67a5c4`.
Authenticated OTA over the real WG address now passes: refinement deployment,
boot, chosen-account re-login and preserved capture/network configuration.
The missing-mount fixture is also installed and restored through WG.
Actual power interruption
during flash and arbitrary non-project image health are not established here.
