# Flash and OTA budget

Checked table: `partitions/xiao_8mb_ota.csv`. It preserves the selected core's
`default_8MB.csv` values. It is configured explicitly in the baseline environment.

| Partition | Offset | Bytes | Purpose |
| --- | --- | ---: | --- |
| nvs | 0x9000 | 20,480 | Small persistent config |
| otadata | 0xe000 | 8,192 | Select active OTA slot |
| app0 | 0x10000 | 3,342,336 | OTA application 0 |
| app1 | 0x340000 | 3,342,336 | OTA application 1 |
| spiffs | 0x670000 | 1,572,864 | Retained flash filesystem partition |
| coredump | 0x7f0000 | 65,536 | Retained diagnostic partition |

The last partition ends exactly at 8,388,608 bytes. Bootloader/partition-table
regions below 0x9000 are reserved. Both app offsets meet 64 KB alignment.

Release budget: **at most 80% of either app slot**, i.e. 2,673,868 bytes for the
application binary. This leaves at least 668,468 bytes of slot headroom.
The checker enforces alignment, overlap, flash boundary, NVS, OTA metadata,
both slots and the binary size limit. Tests include overlap and missing-slot
failures, and rejecting 80.1% even though it physically fits.

| Phase | Firmware bytes | Slot utilization |
| --- | --- | --- |
| Upstream PlatformIO baseline | 1,808,720 | 54.12%; PASS against 80% limit |
| XIAO SPI / USB / safe-startup baseline (2026-10-03) | 1,785,680 | 53.43%; PASS |
| Verified audio / AP / AVI profile (2026-10-03) | 1,823,584 | 54.56%; PASS |
| Verified minimal audio profile (2026-10-03) | 1,821,360 | 54.49%; PASS |
| Current restored minimal profile | 1,821,376 | 54.49%; PASS |
| Embedded camera/auth development profile | 1,830,560 | 54.77%; PASS |
| Camera/auth RAM fixture | 1,825,856 | 54.63%; PASS |
| Camera-integrated WireGuard | Not implemented | Unknown |

The current minimal profile leaves 1,520,960 bytes free in each app slot, with 852,492
bytes remaining below the 80% ceiling. The actual application binary, rather
than the linker's flash summary or the factory image, drives the budget check.
Original audio OTA passed app0 → app1 → app0; minimal OTA also passed app0 → app1.
Actual running partitions and post-boot HTTP were checked. Authenticated product OTA remains pending. SPIFFS being reserved does
not itself provide recovery UI. The new development profile embeds its login
and camera dashboard independently of SD; first setup, missing-card behavior
and the final recovery flow remain acceptance gates.
