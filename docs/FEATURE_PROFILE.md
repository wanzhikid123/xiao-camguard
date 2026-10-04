# Feature profile

The verified minimal profile is `xiao-minimal`, extending the audio baseline.
It preserves the upstream camera/capture/SD buffering/AVI/web/OTA paths and
removes unused intercom and NVR audio paths at compile time. The default
environment remains `xiao-baseline` for a reproducible upstream comparison.

| Subsystem | Minimal profile |
| --- | --- |
| Camera / HTTP MJPEG / manual AVI / SD / web / OTA | Retained and hardware-tested |
| PDM microphone | Enabled; GPIO41 DATA / GPIO42 CLK, 16 kHz mono PCM |
| Browser microphone / intercom / amplifier | Excluded by INCLUDE_INTERCOM=false |
| NVR audio stream / buffer | Excluded by INCLUDE_NVR_AUDIO=false |
| NVR video/subtitles | Disabled at startup; legacy flags cannot re-enable them |
| FTP/HFS, Telegram, SMTP, MQTT, Home Assistant | false |
| Certificates, UART, telemetry, WebDAV, external heartbeat | false |
| Photogrammetry, MCPWM, RTSP, DS18B20, autofocus | false |
| New JPEG decoder, I2C devices, TinyML / optional peripherals | false |
| Motion / continuous recording | Core retained; disabled at XIAO startup |
| Auto-delete / mount-failure format | OFF |

## Real comparison and regressions

| Measurement | Audio baseline | Minimal |
| --- | ---: | ---: |
| Application binary | 1,823,584 B | 1,821,360 B |
| OTA slot use | 54.56% | 54.49% |
| Static RAM | 99,592 B | 99,584 B |
| Observed task count | 18 | 18 |
| Example setup free heap | 154,504 B | 153,268 B; 152,800 B after OTA |
| Example free PSRAM | 3,711,388 B | 3,711,348 B |

The binary is 2,224 bytes smaller. Runtime observations were taken on separate
boots with different network/camera allocations; they do not demonstrate a
net heap improvement. No task reduction or long-term memory claim is made.

After the camera authentication integration, the restored minimal build is
1,821,376 bytes, static RAM 99,584 bytes. Its async-stream cleanup changes add
16 bytes; the comparison and OTA/AVI evidence above describe the earlier
verified binary. Post-restore camera/SD/PDM and HTTP are checked again.

Minimal hardware checks passed on 2026-10-03:

- Root UI, a complete still JPEG and three HTTP MJPEG frames; all image decodes.
- A real 20 s AVI download, MJPEG 640×480 and 16 kHz mono PCM; whole-file decode.
- Video 20.091 s, audio 20.160 s: 69 ms duration difference.
- All four physical 880 Hz stimulus pulses detected; on/off amplitude ratio 5.71.
- OTA from app0 to app1, followed by camera/SD/PDM startup and HTTP access.

The first host restart probe missed Windows' reconnect window. The board log
showed normal initialization; a fresh visit passed. The bounded host harness
now retries its temporary AP profile after a board restart. Normal HTTP reset
also cleared the inherited upstream RTC crash-loop latch.

## Board-specific adaptations retained

Sense SD uses SPI CS21/SCK7/MISO8/MOSI9. GPIO21 is not a lamp output.
The owner's empty exFAT card was explicitly formatted FAT32 and tested; normal
firmware does not format on mount failure. See [Seeed SD documentation](https://wiki.seeedstudio.com/xiao_esp32s3_sense_filesystem/).

The observed OV3660 needs 10 MHz XCLK for decodable JPEGs. Manual startup is
also written into the config vector, which the recorder reloads. Two narrow
AVI header corrections preserve sample length and the measured video clock.

The minimal profile's upstream web assets are still on SD. The separate
`xiao-appliance` development profile now embeds login/dashboard assets and
protects camera/recording APIs. Initial setup, missing-SD recovery, bounded
media enumeration and integrated WireGuard remain product work. Speech quality
and long-duration stress are also unverified.
