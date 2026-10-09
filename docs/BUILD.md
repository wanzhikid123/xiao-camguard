# Build and local tools

Latest release evidence (2026-10-09): [CLOCK_SYNC.md](CLOCK_SYNC.md).
The event AVI clock pipeline is recorded in [EVENT_AVI_CLOCK.md](EVENT_AVI_CLOCK.md).
The initial idle repair is recorded in [EVENT_SESSION_FIX.md](EVENT_SESSION_FIX.md).
Build `xiao-appliance` for normal use; the combined
`xiao-appliance-event-codec-test` environment is for isolated regression only.
Build environments sequentially and archive each BIN/ELF before switching.
Firmware sizes and hashes below dated 2026-10-04 are historical.

These commands use the repository root. Arduino IDE is not required.
Python 3.10–3.14 is required by the selected pioarduino platform (3.14.5 used).

```powershell
python -m pip install --target .tools/python -r requirements-tools.txt
python scripts/pio.py --version
python scripts/pio.py device list --json-output
python scripts/pio.py run -e xiao-baseline
```

All project tool packages, caches and build output go into ignored `.tools/`
and `.pio/`. The Python launcher does not depend on PowerShell script execution.
The optional `.ps1` wrapper delegates to the same launcher.

Build status (2026-10-04): **normal PlatformIO compile/link/upload passed**.
The no-audio SPI/USB startup build measured 1,785,680 bytes, 53.43% of an OTA slot. The audio comparison is built separately; see TEST_REPORT.md for current image size. COM3 is confirmed as the connected XIAO; flash/PSRAM and independent
camera/PDM tests passed. The card is FAT32 and read/write tested. HTTP preview,
audio AVI/download and two OTA transitions passed. See TEST_REPORT.md for evidence.

Current `xiao-appliance` event/media normal image: **1,916,528 bytes (57.34% of
3,342,336)**, static RAM **103,644**, archived privately as
`.tools/hardware/appliance-events-final.bin` with its ELF and digest.
SHA-256: `18b9fb47376bca344a0de857c89ab0227435efc692c646914315e26d3ba67682`.
It is installed by authenticated LAN OTA. The corresponding event fixture is
**1,917,760 bytes (57.38%)**, static RAM **103,652**, built with
`python scripts/pio.py run -e xiao-appliance-event-test`. Its extra authenticated
LAN detector substitutions exist only for acceptance; never install a fixture
for normal use. See [event acceptance](EVENT_CAPTURE.md) for current results.
The normal build takes 177.61 seconds and the fixture 195.58 seconds in the
final recorded build. The exact normal image passes 80 browser/HTTP/media checks
and is restored after the 58-check event integration. Fixture SHA-256:
`c9cb4e43af2f22bb3fcd7bc458b64b6a2e4dd77bb6f47c922af8f9eb83669cdb`.
The former WG-B image `appliance-full-ui-final.bin` (1,894,496 bytes) and its
35/42-check production reports remain historical evidence. Network mode,
revision 71 and the full-tunnel policy remain unchanged in the event image.

The user completed dependency installation. Device Guard then blocked the
generated `esptool.exe` launcher with error 4551. The same virtual environment's
`python.exe -X utf8 -I -m esptool version` succeeds with version 5.4.0.
`scripts/esptool_module.py` now uses that module entry point for bootloader/app
images, serial upload/erase, and the platform's factory-image hook. No Windows
policy, ACL or package version was changed. The factory
hook replacement is guarded against changes in the pinned platform.

An earlier restricted session's Core fallback requested an inaccessible SCons
URL (WinError 10013). The normal Core command now works; this installed-tool
fallback remains available for build-only use:

```powershell
python scripts/build_cached.py
```

It uses installed SCons 4.11.1, the baseline environment and checked-in platform
configuration. It does not install dependencies, upload or erase. Use the
normal `pio.py run` command when dependencies need installing.

| Component | Compile-verified version / setting |
| --- | --- |
| Platform | `pioarduino 55.03.312-1` release ZIP |
| Arduino core | `3.3.12`, header inspected |
| ESP-IDF | `5.5.5`, release metadata |
| Xtensa toolchain | `14.2.0+20260121`, pinned platform manifest |
| Xtensa GDB | `17.1.0+20260402` |
| esptool | `5.4.0` |
| SCons | `4.11.1` (package `4.41101.0`) |
| PIO Home | `3.4.4` |
| Board | `seeed_xiao_esp32s3` |
| Flash / PSRAM mode | QIO / OPI, 8 MB flash target |
| Partition table | `partitions/xiao_8mb_ota.csv` |

The platform pin compiles and boots on the target; original preview, audio AVI and two OTA transitions pass. Final product/stress validation remains open.
There are no unpinned external app libraries in the baseline. Image inspection
reports ESP32-S3, 8 MB, 80 MHz and valid checksums/hashes for bootloader and app.
The image header uses DIO as selected by the platform's QIO conversion; memory
type remains configured as `qio_opi`.

Run standalone host checks:

```powershell
python -m unittest discover -s tests -v
python scripts/check_flash_budget.py
python scripts/check_flash_budget.py --bin .pio/build/xiao-baseline/firmware.bin
python scripts/validate_avi.py path/to/recording.avi
```

The firmware size checker also runs automatically after a successful build and
fails above 80% of the smaller OTA slot. Missing `.bin` is an error, not a pass.
The partition-only command explicitly reports `firmware_checked: false`.

The actual target was confirmed on COM3 and these normal uploads succeeded:

```powershell
python scripts/pio.py run -e xiao-baseline -t upload --upload-port COM3
python scripts/pio.py device monitor --port COM3 --baud 115200
```

Reconfirm the port if USB enumeration changes. The baseline profile retains
the upstream interface and narrow XIAO SPI/USB/startup adaptations. The separate
`xiao-appliance` profile includes persistent admin/network configuration,
authenticated embedded three-language UI, WG-A/WG-B, photo/recording settings and
OTA; see [USER_GUIDE.md](USER_GUIDE.md). Upload separate images,
as PlatformIO does, rather than the factory image to preserve existing NVS.

Sources: [pioarduino release](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.312-1),
[board definition](https://docs.platformio.org/en/latest/boards/espressif32/seeed_xiao_esp32s3.html),
[esptool Python entry point](https://docs.espressif.com/projects/esptool/en/latest/esp32/installation.html).

Audio comparison commands:

```powershell
python scripts/pio.py run -e xiao-audio-baseline
python scripts/pio.py run -e xiao-audio-baseline -t upload --upload-port COM3
```

The wrapper now propagates PlatformIO's error code. A deliberate nonexistent environment exits 1. Use normal build/upload commands; a combined nobuild/upload attempt failed before writing because the pinned platform omitted flash argument pairs. Diagnostics, the explicitly authorized formatter and public-asset provisioning utilities are separate environments under experiments/hardware.
