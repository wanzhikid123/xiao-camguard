# Hardware baseline — core checks pass, 2026-10-03

The owner authorized direct testing of the connected XIAO ESP32S3 Sense and then authorized formatting the empty SD card. COM3 identifies ESP32-S3 rev 0.2, MAC `e0:72:a1:fb:4c:08`, 8 MB flash and 8 MB PSRAM. The owner confirmed the external antenna is attached.

| Check | Observed result |
| --- | --- |
| Boot / memory | QIO flash / OPI PSRAM, both 8 MB; audio setup free heap about 152 KB / PSRAM 3.71 MB |
| SD | Sense SPI CS21/SCK7/MISO8/MOSI9; FAT32 mount, complete 256 KB readback and real AVI writes/downloads pass |
| Camera | OV3660; captured 20 MHz JPEG failed decode; 10 MHz independent and original HTTP captures decode |
| Network / preview | AP discovered and connected; root page, still and three MJPEG frames pass |
| PDM | I2S0, GPIO42 CLK / GPIO41 DATA, mono 16 kHz 16 bit |
| Original manual recording | Post-fix 20 s AVI: MJPEG 640×480 and PCM; whole-file decode passes |
| Audio/video timing | Video 20.109 s / audio 20.160 s; difference 51 ms |
| Two-minute AVI | Video 120.000 s / audio 119.936 s; difference 64 ms; 8,986,062 bytes; full decode |
| Physical mic capture | Four 880 Hz speaker pulses measured in recorded PCM; speech/noise quality unverified |
| OTA | app0 → app1 → app0, actual partition logs plus post-boot HTTP/camera/SD verification |
| PC network restoration | Original Ouyuan5G restored after every temporary AP test; test profile removed |

The current firmware is `xiao-audio-baseline`, application 1,823,584 bytes, SHA-256 `e450dd386ac2a4ac8163171994e84e3a0b2f193d52f01c3d0f32db4fdfcd96b8`. It is 54.56% of either 3,342,336-byte OTA slot.

## Board and upstream corrections

- Native USB CDC needs Serial.write for application logs; IDF stdout remained on UART0.
- Sense SD uses SPI; GPIO21 is card CS and is no longer exposed as a lamp.
- The actual exFAT card was reformatted once with explicit authorization. FAT32 partition now starts at LBA 63. Capacity is 127,865,454,592 bytes, about 128 GB, larger than Seeed's recommended range. Tested results apply to this card.
- XIAO uses measured 10 MHz XCLK, including old configs that contain 20 MHz.
- Motion, timelapse, dashcam, automatic formatting/deletion are disabled at startup, including the later configuration-vector reload.
- A stale AP_START flag survived first-call AP.end(), leaving actual mode STA-only. Checking driver mode and AP creation success restored discovery, DHCP and HTTP access.
- AVI audio dwLength used byte count with a sample time base. It now uses the 16-bit mono sample count.
- AVI video FPS was rounded to an integer, producing duration drift at low light/frame rates. The header now preserves measured frame count and elapsed milliseconds; timelapse keeps its configured rate.

The original capture, SD buffering and AVI mux are retained. These two narrow header fixes were driven by real board recordings, not synthetic success claims. Frame rate depends on lighting and load; measured clips ranged roughly 5–14 fps despite a 20 fps request.

## Preservation and recovery

Full 8 MB flash was backed up before firmware changes to ignored `.tools/hardware/before-baseline-flash.bin`, SHA-256 `4f3b36fa803a07d56dcb11341b629f2ab932066383268e0fe4830d8a31de2cd0`. NVS was preserved during serial uploads. No full erase, factory-image upload or eFuse write.

Unchanged public UI assets were seeded onto SD with size/MD5 readback checks. This is baseline provisioning, not the final embedded recovery UI. Intentional resets before setup completed left upstream's RTC latch set; its verified RAM word was cleared once. The production crash guard remains enabled. Subsequent restarts and OTA use the upstream controlled restart API.

Private logs/media stay under `.tools/hardware/`; source-only diagnostic and host harnesses are in `experiments/hardware/`. See [TEST_REPORT.md](TEST_REPORT.md) for exact files, regressions and limits.

Remaining final acceptance: subjective speech/noise, longer recording drift, 30+ minutes idle, 20 record cycles, storage full/missing SD, Wi-Fi/WG reconnect, NVS rollback, protected endpoints, authenticated LAN/WG OTA and full-tunnel packet/routing evidence.

Primary references: [Sense SD](https://wiki.seeedstudio.com/xiao_esp32s3_sense_filesystem/), [Sense camera](https://wiki.seeedstudio.com/xiao_esp32s3_camera_usage/), [Sense microphone](https://wiki.seeedstudio.com/xiao_esp32s3_sense_mic/).
