# CODEX PLAN v2 — XIAO ESP32S3 Sense WireGuard Camera

## 0. Mission

User-approved extension (2026-10-04): independent **640×480 event clips** with
synchronized PDM audio and discarded confirmed-static tails; media checkboxes,
delete-selected and delete-all across date/file pagination. Preserve ordinary
manual recording, stored settings and existing media during acceptance. The
user is unavailable for physical motion tests, so automated detector/recording
integration and physical sensitivity evidence must be reported separately.
Implementation and verification scope: [EVENT_CAPTURE.md](docs/EVENT_CAPTURE.md).

Build a robust camera appliance for **Seeed Studio XIAO ESP32S3 Sense** using **PlatformIO-compatible tooling + Arduino-ESP32 3.x**, starting from a proven camera/SD/audio project rather than rebuilding the capture stack from scratch.

Primary goals:

1. Use a reliable existing camera project as the base.
2. First-run web setup for Wi-Fi, admin password, and WireGuard.
3. Persist configuration in ESP32 **NVS** via Arduino `Preferences`.
4. Support:
   - remote access to the camera through its WireGuard IP;
   - full-tunnel/default-route operation where normal outbound traffic uses WireGuard when configured with `AllowedIPs = 0.0.0.0/0`.
5. Manual **photo capture** and **video recording with sound** in v1.
6. Store media on the XIAO Sense **microSD** card.
7. Show **ESP32-S3 chip temperature** and other useful device metrics.
8. Preserve OTA updating.
9. Replace/upgrade the web UI so it looks modern, polished, responsive, and appliance-like.
10. Keep local recovery possible even when WireGuard or Wi-Fi configuration is wrong.
11. Preserve recording reliability above cosmetic or secondary features.

Do not claim a feature works if it is not verified on the actual pinned Arduino-ESP32 3.x networking stack.

---

## 1. Agreed Product Requirements

### Hardware

Target:

- Seeed Studio XIAO ESP32S3 Sense
- ESP32-S3
- 8 MB flash
- 8 MB PSRAM
- Camera on Sense expansion board
- PDM microphone on Sense expansion board
- microSD slot on Sense expansion board

Useful official pin references:

- PDM microphone DATA: GPIO 41
- PDM microphone CLK: GPIO 42
- microSD CS: GPIO 21
- microSD SCK: GPIO 7
- microSD MISO: GPIO 8
- microSD MOSI: GPIO 9

Do not redefine camera/microphone/SD pins without first checking the XIAO ESP32S3 Sense board definition and upstream implementation.

### First release

Implement:

- live camera view;
- manual JPG photo capture;
- manual Start Recording / Stop Recording;
- MJPEG AVI video;
- microphone audio in the recorded video;
- microSD media storage;
- media list / preview / playback where practical / download / delete;
- Wi-Fi onboarding;
- WireGuard onboarding;
- admin authentication;
- rescue AP;
- WireGuard status;
- chip temperature;
- Wi-Fi RSSI;
- LAN IP;
- WireGuard IP;
- uptime;
- free heap;
- PSRAM status/usage;
- SD total/free space;
- OTA update;
- polished mobile/desktop web interface.

Do **not** make motion detection or continuous recording a first-release requirement.

Preserve upstream capability where possible, but do not expand or redesign motion/continuous recording until manual recording is stable.

### Default media settings

Start with:

- video resolution: **640 × 480**
- target frame rate: **15 FPS**
- video codec: **MJPEG**
- audio: **16 kHz, 16-bit, mono PCM**
- still images: JPEG

Allow resolution, FPS, and JPEG quality to become configurable later through the UI without destabilizing the first implementation.

### Runtime priority order

When resources are constrained, prioritize:

1. **recording integrity**
2. camera capture
3. SD write stability
4. audio capture
5. WireGuard connectivity
6. live preview smoothness
7. telemetry/status refresh
8. UI polish/animation

Do not let dashboard refreshes, thumbnails, cosmetic effects, or live preview smoothness corrupt or destabilize recording.

---

## 2. Base Repository

Preferred upstream:

`https://github.com/s60sc/ESP32-CAM_MJPEG2SD`

Reasons:

- existing XIAO ESP32S3 Sense support;
- existing ESP32-S3 camera support;
- existing microSD recording;
- existing PDM microphone/audio support;
- existing AVI recording;
- existing browser streaming and file transfer;
- existing first-run AP configuration;
- existing OTA functionality;
- actively maintained;
- already designed around modern ESP32-S3 hardware.

Important license note:

- The base project is AGPL-3.0.
- Preserve all license files and notices.
- Do not remove attribution.
- Treat the resulting firmware as a derivative work unless architecture changes later.
- Do not spend time changing licenses.

Git workflow:

1. Clone upstream.
2. Record the exact upstream commit SHA in `docs/UPSTREAM.md`.
3. Create a tag or branch named `upstream-baseline`.
4. Create the working branch, e.g. `feature/xiao-wg-ui`.
5. Keep the initial build-system adaptation as small as possible.
6. Do not perform broad refactors before a clean baseline build exists.

---

## 3. PlatformIO Strategy — IMPORTANT

The upstream project requires modern **Arduino-ESP32 3.x**.

Do **not** assume the stock stable `platform = espressif32` package provides the required Arduino core.

Preferred approach:

- use a PlatformIO-compatible **pioarduino** platform that supports Arduino-ESP32 3.x;
- first find a known-good stable combination by compiling the upstream project;
- once verified, pin the exact platform release or commit/archive URL;
- do not leave the platform floating on `latest`, `master`, or an unpinned branch.

The final project should use a reproducible PlatformIO configuration.

Conceptually:

```ini
[env:seeed_xiao_esp32s3]
platform = <PINNED_PIOARDUINO_PLATFORM_RELEASE_OR_COMMIT>
board = seeed_xiao_esp32s3
framework = arduino
```

Do not hardcode a guessed version before verification.

### Environment ownership

Codex should install/check the environment itself.

Required:

- Git
- Python 3.9+
- PlatformIO Core
- serial terminal capability
- optional `ffprobe` / FFmpeg for validating AVI audio/video streams

Prefer an isolated Python environment or `pipx` when practical.

Do not require Arduino IDE.

Verify:

```bash
git --version
python --version
pio --version
pio device list
```

Before implementing product features, verify at boot:

- flash size;
- PSRAM detected;
- PSRAM size non-zero;
- camera init success;
- microSD init success;
- microphone init success.

---

## 4. Build-System Migration Rule

The upstream project is Arduino-oriented.

Convert it to PlatformIO-compatible building with **minimal source churn**.

Order:

1. Make the unmodified upstream source compile with the chosen pioarduino/PlatformIO setup.
2. Fix only build-system and board-definition differences.
3. Flash it.
4. Verify existing camera stream, SD, recording, microphone, and OTA functionality.
5. Commit this state as `baseline-platformio`.
6. Only then begin feature work.

Do not simultaneously:

- convert build system;
- refactor modules;
- redesign UI;
- add WireGuard;
- change recording internals.

If the upstream source layout can be compiled by configuring PlatformIO around the existing layout, prefer that first.

A later cleanup into `src/`, `include/`, `lib/`, etc. is allowed only after baseline verification.

---

## 5. Minimal Feature Profile — REQUIRED BEFORE NEW FEATURES

MJPEG2SD includes many optional features that this project does not need.

After verifying the untouched baseline, create a **minimal feature profile** before adding WireGuard.

Disable nonessential upstream features wherever the build configuration supports it.

Keep only what is required for this product:

- camera;
- PDM audio;
- SD/media;
- web server;
- first-run setup/recovery;
- OTA;
- configuration system;
- required logging/debugging;
- any dependency needed by the above.

Prefer disabling optional features such as, where present:

- MQTT;
- Home Assistant;
- Telegram;
- email;
- FTP;
- WebDAV;
- intercom;
- vehicle remote features;
- unrelated sensors;
- unrelated protocols/services;
- optional cloud integrations;
- other unused subsystems.

Do not delete large amounts of upstream code if simple feature flags can disable it.

Goals:

- reduce firmware size;
- reduce heap pressure;
- reduce runtime task count;
- reduce network-stack interactions;
- create headroom for WireGuard, auth, and upgraded UI.

Record the resulting enabled/disabled feature profile in:

```text
docs/FEATURE_PROFILE.md
```

---

## 6. Flash Partition and OTA Budget — REQUIRED EARLY

The XIAO ESP32S3 Sense has **8 MB flash**.

OTA requires enough flash for a second application image slot.

Do not postpone partition planning until the end.

### Requirements

Before major new features:

1. inspect the current firmware binary size;
2. inspect the partition table;
3. select or create an **8 MB partition layout with dual OTA app slots**;
4. keep NVS and required filesystem/data partitions;
5. ensure each OTA slot can hold the final firmware with safe margin.

Target rule:

- normal release firmware should remain below approximately **80–85% of OTA slot size**.

Codex must report firmware size after major phases.

If the UI grows too large:

- gzip static assets where supported;
- remove unused assets;
- minimize JS/CSS;
- avoid heavy frameworks;
- do not sacrifice OTA capability for visual polish.

### UI storage rule

Basic admin/setup UI must be available from firmware even if the SD card is absent or corrupt.

Do not make the only usable configuration UI depend on microSD.

A richer UI may optionally use additional SD assets, but a minimal embedded recovery UI must always remain available.

---

## 7. What NVS Means

Use ESP32 **NVS (Non-Volatile Storage)** through Arduino's `Preferences` library.

NVS is persistent flash-backed key/value storage. Data remains after reset and power loss.

Use it for small configuration values such as:

- Wi-Fi SSID/password;
- WireGuard settings;
- admin password verifier;
- settings flags;
- device name;
- capture preferences;
- config version;
- boot/recovery metadata.

Do **not** use EEPROM emulation for this project.

Do not store photos, videos, logs, or large web assets in NVS.

Possible namespaces:

```text
wifi
wg
auth
app
cfg
```

Suggested keys:

```text
wifi:
  ssid
  pass

wg:
  en
  addr
  priv
  pub
  psk
  host
  port
  allow
  keep
  dns
  mtu
  full

auth:
  salt
  hash
  iter

app:
  name
  autodel
  minfree

cfg:
  ver
  active
  pending
```

The exact schema can change if the existing upstream configuration architecture makes another design cleaner.

Provide a migration path/version number if the stored format changes.

---

## 8. Last Known Good / Pending Configuration

Do not immediately replace a known-working network configuration with a new unverified one.

Use a **pending → validated → active** flow.

Conceptually:

1. existing active config remains intact;
2. user submits new Wi-Fi/WG config;
3. write it as pending;
4. validate syntax;
5. attempt connection;
6. if validation succeeds, promote pending to active;
7. if validation fails, keep/recover previous active config.

For first boot where no active config exists, setup mode remains available.

This is especially important for:

- Wi-Fi credential changes;
- WG endpoint changes;
- WG route changes;
- full-tunnel configuration.

---

## 9. Secret Handling

Ordinary NVS is persistent, but should **not** be described as hardware-secure secret storage.

Requirements:

- never print Wi-Fi passwords;
- never print WireGuard private keys;
- never print preshared keys;
- never print admin password material;
- never return stored WireGuard private keys in status APIs;
- mask secrets in the UI;
- do not commit secrets to Git;
- add common secret/config exports to `.gitignore`;
- do not enable eFuse/secure boot/flash encryption automatically;
- do not make irreversible security changes without explicit user approval.

---

## 10. First-Boot Onboarding

If no valid Wi-Fi config exists:

1. Start setup AP.
2. Serve setup UI at `192.168.4.1`.
3. If practical, add captive-portal DNS behavior, but do not make it a blocking dependency.
4. Require the user to create an **admin password** during initial setup.
5. Allow entry of:
   - Wi-Fi SSID;
   - Wi-Fi password;
   - WireGuard enabled/disabled;
   - WireGuard configuration.
6. Validate before writing.
7. Store as pending config.
8. Apply safely.
9. Connect to Wi-Fi.
10. Sync time.
11. Start WireGuard if enabled.
12. If successful, promote config to active.
13. Keep local LAN UI available.

Do not require internet access to render the setup UI.

No CDN dependency.

---

## 11. Rescue / Recovery Behavior

A bad VPN configuration must never permanently lock the user out.

### Wi-Fi failure

If configured Wi-Fi cannot connect for approximately **60 seconds**:

- enter rescue/setup AP mode;
- expose the local configuration UI;
- allow Wi-Fi and WireGuard settings to be repaired.

### WireGuard failure

If Wi-Fi works but WireGuard fails:

- keep normal LAN UI reachable;
- show the WireGuard error;
- provide `Restart WireGuard`;
- provide `Disable WireGuard`;
- provide `Clear WireGuard Config`;
- do not reboot-loop;
- do not disable camera/recording functionality.

### Rescue AP lifetime

If rescue AP was entered because an otherwise configured system failed:

- do not leave it permanently open;
- close it when normal Wi-Fi is restored;
- optionally auto-close it after roughly **10 minutes of inactivity**, provided recovery remains possible.

### Optional physical recovery

If reliable and conflict-free:

- allow holding BOOT during startup to force rescue AP.

Document exact behavior.

---

## 12. Authentication and Security Model

Admin authentication is required.

### Password storage

- username may be fixed as `admin`;
- password is chosen during setup;
- never store plaintext password;
- use a salted password verifier;
- prefer PBKDF2-HMAC-SHA256 using available mbedTLS;
- if impractical with the pinned framework, use a documented secure supported alternative;
- do not invent custom cryptography.

### Session behavior

- login page;
- authenticated session;
- logout;
- `HttpOnly` cookie if supported;
- `SameSite=Strict` if supported;
- state-changing endpoints require authentication.

### What requires authentication

Require login for:

- live camera;
- still image capture;
- recording controls;
- media listing;
- media download;
- media deletion;
- status API;
- settings;
- OTA;
- WireGuard controls.

Exceptions:

- initial setup UI when no admin account exists;
- controlled rescue/recovery path as needed.

### Transport security model

Do not assume password hashing makes HTTP transport confidential.

For v1:

- **do not expose the device directly to the public internet**;
- do not recommend router port forwarding;
- remote access should occur through WireGuard;
- LAN HTTP is treated as trusted-LAN access;
- HTTP inside WireGuard is protected by the WG tunnel.

HTTPS may be evaluated later, but must not destabilize the ESP32-S3 camera workload.

If upstream HTTPS is experimentally usable, do not make it a blocker for v1.

---

## 13. WireGuard Configuration UX

Support **both** configuration methods.

### Method A — Paste standard WireGuard config

Provide a textarea for a normal `.conf`.

Example only:

```ini
[Interface]
PrivateKey = ...
Address = 10.x.x.x/32
DNS = ...
MTU = 1280

[Peer]
PublicKey = ...
PresharedKey = ...
Endpoint = host.example.com:51820
AllowedIPs = 0.0.0.0/0
PersistentKeepalive = 25
```

Never include real keys in repository examples.

Parse and validate it.

### Method B — Individual form fields

Expose:

- enabled;
- Address/CIDR;
- PrivateKey;
- Peer PublicKey;
- PresharedKey if backend support is verified;
- Endpoint hostname/IP;
- Endpoint port;
- AllowedIPs;
- PersistentKeepalive;
- DNS;
- MTU.

The paste view and field view should represent the same structured model.

Buttons:

- `Validate`
- `Save`
- `Save & Connect`
- `Restart WireGuard`
- `Disable`
- `Clear Config`

### v1 scope

Support:

- one WireGuard peer;
- IPv4 first.

Reject unsupported configs clearly.

Do not silently ignore:

- multiple peers;
- unsupported IPv6 routing;
- unsupported PSK behavior;
- unsupported route semantics;
- unsupported MTU fields.

---

## 14. WireGuard Compatibility Spike — HARD GATE

Primary candidate:

`https://github.com/ciniml/WireGuard-ESP32-Arduino`

The public release is old relative to current Arduino-ESP32 networking changes.

There is newer work toward ESP-NETIF compatibility.

Relevant PR to inspect:

`https://github.com/ciniml/WireGuard-ESP32-Arduino/pull/52`

### Spike procedure

Create a tiny independent PlatformIO test:

```text
experiments/wireguard/
```

It should only:

1. connect to Wi-Fi;
2. sync NTP time;
3. start WireGuard;
4. reach one IP/service through the tunnel;
5. print sanitized state;
6. stop/restart WireGuard safely;
7. reconnect multiple times;
8. verify memory does not degrade catastrophically across reconnects.

Use the **same pinned Arduino-ESP32 3.x/pioarduino environment** intended for the camera firmware.

### Decision order

1. Test released WireGuard library.
2. If incompatible, inspect current `main`.
3. If needed, test the ESP-NETIF migration PR/fork.
4. If using unreleased code, pin exact commit SHA.
5. Prefer a small local compatibility wrapper over a large invasive fork.
6. Do not downgrade the whole camera stack to an obsolete Arduino core just to make WireGuard compile.

Document:

```text
docs/WIREGUARD_BACKEND.md
```

Include:

- repository;
- exact version/commit;
- reason selected;
- known limitations;
- what “Connected” means;
- whether handshake information is available;
- whether AllowedIPs/default-route manipulation is actually supported.

---

## 15. WireGuard Delivery in Two Milestones

Do not make full-tunnel routing block the rest of the product.

### WG-A — Remote access milestone

Must achieve first:

- Wi-Fi works;
- time sync works;
- WireGuard starts;
- device gets/uses WG address;
- camera UI is reachable over WG IP;
- LAN UI remains reachable;
- reconnect works;
- failure does not break LAN access.

This milestone must be stable before continuing major networking work.

### WG-B — Full-tunnel milestone

Only after:

- camera;
- manual recording;
- auth;
- setup/recovery;
- basic UI

are stable.

Then implement:

- `AllowedIPs` routing;
- `0.0.0.0/0` default route;
- endpoint exception;
- local LAN exception;
- DNS behavior;
- no route loop;
- truthful tunnel state.

Full tunnel remains an explicit final requirement, but is not allowed to block the core camera product.

---

## 16. WireGuard Runtime State Model

Implement WG lifecycle cleanly.

Possible states:

```text
DISABLED
WAITING_FOR_WIFI
WAITING_FOR_TIME
STARTING
ACTIVE
HANDSHAKE_OK      (only if backend exposes real handshake info)
ERROR
```

User-facing:

- Disabled
- Connecting
- Active
- Connected
- Error

Rules:

- only display `Connected` if there is a meaningful verified condition;
- if handshake state is unavailable, say `Active — handshake status unavailable`;
- never fabricate `Last handshake`.

Expose the last sanitized error.

---

## 17. Full-Tunnel Requirement

The user requires both:

1. camera access through WG;
2. outbound traffic through WG when config requests full tunnel.

If:

```text
AllowedIPs includes 0.0.0.0/0
```

attempt to make WG the default route for non-local IPv4 traffic.

### Critical route constraints

Avoid a routing loop.

The WireGuard UDP endpoint itself must remain reachable via the Wi-Fi underlay.

Preserve direct routes for:

- local LAN subnet;
- Wi-Fi gateway;
- WireGuard endpoint;
- bootstrap DNS/NTP where needed before tunnel establishment.

Recommended sequence:

1. connect Wi-Fi;
2. get DHCP address/gateway/DNS;
3. resolve WG endpoint;
4. sync NTP;
5. establish WG interface;
6. preserve endpoint route via Wi-Fi;
7. preserve local LAN route via Wi-Fi;
8. apply WG AllowedIPs;
9. only then change default route if requested;
10. validate actual routing.

Do not assume a high-level `begin()` call provides sufficient route control.

Inspect actual lwIP / ESP-NETIF behavior used by the chosen backend.

### Leak behavior

When full tunnel is enabled:

- do not silently fall back to normal WAN traffic and still claim full tunnel;
- if strict no-leak behavior cannot be implemented, keep LAN recovery;
- mark full-tunnel mode experimental/incomplete;
- document the limitation honestly.

### Acceptance

With real user WG config:

- camera reachable via WG address;
- camera reachable via LAN;
- endpoint traffic leaves via Wi-Fi underlay;
- ordinary outbound test traffic uses WG;
- no route loop;
- reconnect restores correct routes;
- failed WG does not brick LAN admin access;
- full-tunnel status appears only after route verification.

---

## 18. Camera, Photo, Video, and Audio

Reuse upstream capture/recording code wherever possible.

Do not rewrite the proven camera/AVI pipeline just for style.

### Photo

Provide:

- `Take Photo`

Acceptance:

- valid JPEG;
- saved to microSD;
- appears in media browser;
- downloadable;
- sensible filename/timestamp.

### Manual recording

Controls:

- `Start Recording`
- `Stop Recording`

Show:

- recording state;
- duration.

Defaults:

```text
640x480
15 FPS target
MJPEG
16 kHz / 16-bit / mono PCM
```

### Recording precedence

If recording is active and resources become constrained:

- recording wins over preview;
- preview may lower frame rate or pause;
- telemetry may refresh less frequently;
- media thumbnail generation may be deferred.

Never sacrifice recorded AVI integrity to maintain a smooth dashboard.

### Audio acceptance

Do not assume microphone initialization means the final AVI has working audio.

Validate with host tools.

Acceptance:

- AVI opens in VLC or equivalent;
- video stream present;
- audio stream present;
- audio is audible;
- audio duration tracks video duration reasonably;
- no severe progressive A/V drift in several-minute test.

Use `ffprobe` if available.

---

## 19. Browser Playback Expectations

Do not redesign the recording format merely because browsers have inconsistent native AVI support.

Rules:

- continue using MJPEG + PCM AVI if upstream recording is reliable;
- reuse upstream playback mechanism where practical;
- photos should preview directly;
- videos may use upstream browser playback if available;
- otherwise provide reliable download for VLC/desktop playback.

Do **not** add on-device transcoding.

Do **not** attempt H.264 software transcoding on the ESP32-S3 for this project.

---

## 20. microSD Behavior

On boot show:

- SD mounted/not mounted;
- total size;
- free size;
- used percentage.

If SD is absent/corrupt:

- live view works;
- settings work;
- status works;
- photo/record controls fail gracefully or are disabled;
- no crash/reboot loop.

### Media manager

Provide:

- Photos tab;
- Videos tab;
- date/time;
- file size;
- thumbnail where practical;
- preview/playback where practical;
- download;
- delete.

Avoid loading the entire card directory into RAM.

Use pagination/lazy enumeration where needed.

### Auto-delete

Optional setting:

```text
Auto-delete oldest media when free space < 10%
```

Default:

```text
OFF
```

Deletion must never remove non-media system/config files.

---

## 21. Time and Filenames

Use NTP after Wi-Fi connection.

WireGuard startup may depend on valid time.

Media naming should use wall-clock time when available.

If valid time is unavailable:

- do not block camera use forever;
- use safe fallback filenames from boot counter/uptime;
- switch to wall-clock naming after NTP succeeds.

Expose time sync status.

---

## 22. Chip Temperature and Diagnostics

ESP32-S3 exposes internal chip temperature.

Label it:

```text
Chip temperature
```

or:

```text
ESP32-S3 die temp
```

Do not call it ambient/room temperature.

Use the supported API matching the pinned core.

### Diagnostics to show

Also include:

- reset reason;
- watchdog/brownout clues if available;
- boot count;
- uptime;
- free heap;
- minimum free heap if practical;
- PSRAM total/free;
- SD total/free;
- Wi-Fi RSSI;
- LAN IP;
- WG IP/state;
- current recording state.

### Power note

Document that camera + Wi-Fi + SD + audio + WG can create significant current spikes.

Use a reliable USB cable and power source.

When unexpected resets occur, check:

- brownout;
- watchdog;
- heap exhaustion;
- SD stalls;
- networking failure

before assuming WG logic is the cause.

---

## 23. Status API

Prefer adapting the upstream status/config transport.

Do **not** force a brand-new API architecture if the upstream WebSocket/status path is already appropriate.

If a simple REST endpoint is useful, something like:

```text
GET /api/status
```

is acceptable.

Return only sanitized fields.

Example:

```json
{
  "uptime_s": 0,
  "chip_temp_c": 0.0,
  "reset_reason": "",
  "wifi": {
    "connected": true,
    "ssid": "example",
    "rssi": -55,
    "ip": "192.168.x.x"
  },
  "wireguard": {
    "enabled": true,
    "state": "active",
    "ip": "10.x.x.x",
    "endpoint": "host:51820",
    "full_tunnel": false,
    "last_error": ""
  },
  "memory": {
    "free_heap": 0,
    "min_free_heap": 0,
    "psram_total": 0,
    "psram_free": 0
  },
  "sd": {
    "mounted": true,
    "total_bytes": 0,
    "free_bytes": 0
  },
  "recording": {
    "active": false,
    "duration_s": 0
  }
}
```

Do not include:

- passwords;
- private keys;
- preshared keys;
- password hashes;
- session secrets.

Target UI refresh:

- around every **2 seconds** when idle;
- reduce frequency automatically while recording if needed.

---

## 24. Web UI Redesign

The interface should look modern, premium, and polished.

Target:

- security-camera appliance feel;
- responsive phone/desktop layout;
- clean typography;
- restrained animation;
- strong hierarchy;
- obvious recording state;
- no “Arduino demo page” appearance.

### Suggested layout

#### Dashboard

Top bar:

- device name;
- Wi-Fi badge;
- WG badge;
- recording badge;
- user/settings menu.

Main area:

- large live preview;
- `Take Photo`;
- prominent `Start Recording` / `Stop Recording`;
- recording timer;
- quick camera settings.

Status cards:

- Chip temperature;
- Wi-Fi RSSI;
- LAN IP;
- WG IP;
- WG state;
- SD free space;
- uptime;
- heap;
- PSRAM;
- reset reason.

#### Media

- Photos
- Videos
- date filters
- thumbnails where practical
- download
- delete

#### Settings

Sections:

- Network
- WireGuard
- Camera
- Recording
- Storage
- Security
- System / OTA

### Frontend constraints

Prefer:

- HTML;
- modern CSS;
- vanilla JS;
- inline SVG or local icon assets.

Avoid heavy frameworks unless there is a strong reason.

No external CDN.

Compress static assets where practical.

Basic recovery/admin UI must remain embedded and available without SD.

---

## 25. Architecture Rule — Adapt Before Refactor

Do not force the project into a brand-new module architecture if upstream already has working equivalents.

Preferred principle:

- reuse upstream architecture;
- isolate new WireGuard logic cleanly;
- isolate new auth/config logic cleanly;
- avoid touching proven recording internals.

Possible conceptual boundaries:

```text
ConfigStore
AuthManager
WireGuardManager
StatusService
```

But these names are **not mandatory**.

Do not rewrite:

- CameraManager;
- Recorder;
- MediaStore;
- web transport

unless there is a demonstrated need.

If a new abstraction differs materially from upstream style, document why in:

```text
docs/DECISIONS.md
```

---

## 26. OTA

Preserve upstream OTA.

Requirements:

- authenticated;
- clear progress/error state;
- no secret leakage;
- bootable fallback behavior where supported;
- reachable over LAN;
- reachable over WG after WG-A is stable.

Do not implement automatic firmware update from arbitrary URLs.

### OTA budget checks

At every major release candidate:

- report `.bin` size;
- report OTA slot size;
- report percentage used;
- fail the release checklist if margin is too small.

---

## 27. Logging

Useful logs:

```text
WiFi connected, IP=...
NTP synchronized
WireGuard starting
WireGuard active
SD mounted
Recording started
Recording stopped
OTA started
```

Forbidden:

```text
WiFi password=...
WG private key=...
WG preshared key=...
admin password=...
raw pasted wg.conf with secrets
```

When parser errors occur:

- report line/field;
- do not echo sensitive values.

---

## 28. Reliability Constraints

This device combines:

- camera;
- Wi-Fi;
- SD writes;
- PDM audio;
- WireGuard;
- web server;
- OTA.

Therefore:

- check large allocations;
- use PSRAM appropriately;
- avoid large JSON docs;
- stream media instead of buffering whole files;
- do not read whole videos into RAM;
- avoid long blocking web handlers;
- avoid unnecessary parallel tasks;
- monitor free heap;
- monitor minimum free heap;
- test repeated start/stop recording;
- test recording with WG active;
- test live preview with WG active;
- test SD absence;
- test Wi-Fi reconnect;
- test WG reconnect;
- test 30+ minute idle operation;
- test several-minute recording;
- test repeated reconfiguration.

Do not enable motion/continuous recording work until manual recording passes stability tests.

---

## 29. Implementation Phases

Make small, reviewable commits.

### Phase 0 — Environment + upstream capture

Deliver:

- Git/Python/PlatformIO ready;
- upstream cloned;
- SHA recorded;
- branch created.

Commit:

```text
chore: bootstrap upstream and PlatformIO environment
```

### Phase 1 — Pioarduino/PlatformIO baseline

Deliver:

- identify a working Arduino-ESP32 3.x pioarduino setup;
- compile upstream;
- pin exact platform version/commit;
- select XIAO board;
- avoid feature changes.

Commit:

```text
build: add pinned PlatformIO Arduino 3.x support for XIAO ESP32S3 Sense
```

### Phase 2 — Hardware baseline

Verify:

- boot;
- PSRAM;
- camera;
- live stream;
- SD;
- microphone;
- original recording;
- OTA.

Write:

```text
docs/HARDWARE_BASELINE.md
```

Commit:

```text
docs: record XIAO Sense hardware baseline
```

### Phase 3 — Minimal Feature Profile + OTA budget

Deliver:

- disable unnecessary upstream features;
- choose dual-slot OTA partition layout;
- record firmware/slot size;
- preserve core camera/SD/audio/web/OTA behavior.

Write:

```text
docs/FEATURE_PROFILE.md
docs/FLASH_LAYOUT.md
```

Commit:

```text
build: define minimal feature profile and OTA flash budget
```

### Phase 4 — NVS config + Last Known Good

Implement:

- versioned config;
- active/pending config;
- secret masking;
- recovery behavior;
- config migration.

Commit:

```text
feat: add persistent NVS configuration with rollback
```

### Phase 5 — WG compatibility spike

Independent experiment only.

Deliver:

- compile;
- connect;
- reconnect;
- sanitized status;
- backend decision.

Commit:

```text
test: validate WireGuard backend on pinned Arduino ESP32 3.x
```

### Phase 6 — WG-A remote access

Implement:

- WG parser;
- structured settings;
- lifecycle;
- errors;
- remote access over WG IP;
- LAN-safe behavior;
- reconnect.

Do **not** implement full tunnel yet.

Commit:

```text
feat: add WireGuard remote-access connectivity
```

### Phase 7 — Authentication + setup + rescue

Implement:

- first-run setup;
- admin password;
- login/session;
- rescue AP;
- save/apply/rollback flow.

Commit:

```text
feat: add authenticated onboarding and rescue portal
```

### Phase 8 — Manual camera/media product flow

Verify/refine:

- photo;
- start/stop recording;
- audio;
- SD handling;
- media browser;
- download/delete.

Commit:

```text
feat: finalize manual capture and media workflow
```

### Phase 9 — Modern UI

Implement:

- premium responsive dashboard;
- telemetry;
- settings UI;
- media UI.

Respect recording priority.

Commit:

```text
feat: redesign camera dashboard and admin UI
```

### Phase 10 — WG-B full tunnel

Implement:

- AllowedIPs routing;
- default route;
- endpoint exception;
- LAN exception;
- MTU handling;
- route verification;
- honest tunnel state.

Commit only if verified:

```text
feat: add verified WireGuard full-tunnel routing
```

If incomplete, use a truthful commit message and document limitations.

### Phase 11 — OTA/security hardening

Deliver:

- authenticated OTA;
- size-margin validation;
- secret review;
- recovery review;
- route failure review.

Commit:

```text
feat: harden OTA, recovery, and security behavior
```

### Phase 12 — Stress test + documentation

Create:

```text
docs/TEST_REPORT.md
docs/USER_GUIDE.md
docs/DECISIONS.md
docs/PROGRESS.md
```

Commit:

```text
docs: add validation report and user guide
```

---

## 30. Acceptance Checklist

### Build

- [ ] Fresh clone builds with documented command.
- [ ] Pioarduino/platform version is pinned.
- [ ] Arduino-ESP32 3.x version is known.
- [ ] Dependencies are reproducible.
- [ ] No Arduino IDE dependency.
- [ ] No secrets in Git.

### Flash / OTA

- [ ] 8 MB partition layout documented.
- [ ] Dual OTA slots available.
- [ ] Firmware size reported.
- [ ] Firmware below agreed OTA slot threshold.
- [ ] Recovery UI available without SD.

### Hardware

- [ ] XIAO ESP32S3 Sense boots.
- [ ] PSRAM detected.
- [ ] Camera stream works.
- [ ] SD mounts.
- [ ] PDM mic works.
- [ ] Reset reason readable.

### Minimal profile

- [ ] Unused upstream features disabled.
- [ ] Core camera/SD/audio/web/OTA behavior preserved.
- [ ] Heap improved or at least not regressed badly.

### Setup

- [ ] Empty config => setup AP.
- [ ] Setup page works offline.
- [ ] Admin password required.
- [ ] Wi-Fi persists.
- [ ] WG config persists.
- [ ] pending config can roll back.
- [ ] rescue AP appears after Wi-Fi failure.
- [ ] rescue AP does not remain unnecessarily open.
- [ ] broken WG does not break LAN access.

### Authentication

- [ ] Live view requires login.
- [ ] Photo/record controls require login.
- [ ] Media list/download/delete require login.
- [ ] Status requires login.
- [ ] Settings require login.
- [ ] OTA requires login.
- [ ] No plaintext password stored.

### WG-A

- [ ] `.conf` parser works.
- [ ] Individual fields work.
- [ ] Invalid config gives clear errors.
- [ ] Secrets absent from logs/status.
- [ ] WG starts.
- [ ] WG stops.
- [ ] WG restarts repeatedly.
- [ ] UI reachable over WG IP.
- [ ] UI reachable over LAN while WG active.
- [ ] Wi-Fi reconnect restores WG.

### Camera/media

- [ ] Live preview works.
- [ ] Manual photo works.
- [ ] Start recording works.
- [ ] Stop recording works.
- [ ] AVI contains MJPEG video.
- [ ] AVI contains audible audio.
- [ ] A/V sync acceptable.
- [ ] Download works.
- [ ] Delete works.
- [ ] Missing SD fails gracefully.
- [ ] Recording remains stable while WG active.
- [ ] Preview degradation does not corrupt recording.

### UI

- [ ] Mobile layout usable.
- [ ] Desktop layout usable.
- [ ] No external CDN required.
- [ ] Secrets masked.
- [ ] Recording state obvious.
- [ ] UI responsive during normal use.
- [ ] Telemetry load backs off if recording needs resources.

### WG-B full tunnel

- [x] AllowedIPs behavior verified.
- [x] `0.0.0.0/0` behavior verified or honestly marked incomplete.
- [x] Endpoint route never loops into WG.
- [x] Local LAN route preserved.
- [x] MTU configurable.
- [x] Reconnect restores routes.
- [x] Failed WG does not brick local admin.
- [x] Full-tunnel status only shown after real verification.

WG-B evidence (2026-10-04): [routing, actual physical-output guard and
acceptance boundaries](docs/WIREGUARD_FULL_TUNNEL.md). IPv4 full mode is
implemented; IPv6 WAN is blocked rather than tunneled. External endpoint
migration/key-expiry and final-image remote-peer confirmation are recorded
separately from the passed local/lifecycle/packet/UI gates.

### OTA

- [ ] OTA works.
- [ ] OTA requires auth.
- [ ] OTA from LAN works.
- [ ] OTA over WG works after WG-A.
- [ ] OTA size margin remains acceptable.

### Reliability

- [ ] 30+ minute idle test passes.
- [ ] Several-minute recording test passes.
- [ ] Repeated recording start/stop passes.
- [ ] WG reconnect cycles pass.
- [ ] Wi-Fi reconnect cycles pass.
- [ ] No obvious heap collapse.
- [ ] Brownout/watchdog/reset reason is diagnosable.

---

## 31. Out of Scope for v1

Do not let these delay the first stable release:

- motion-trigger UI refinement;
- continuous 24/7 recording UX;
- AI object detection;
- cloud service;
- multi-camera management;
- multiple WG peers;
- IPv6 WG;
- intercom;
- Home Assistant integration;
- Bluetooth setup;
- native mobile app;
- on-device video transcoding.

If upstream already includes some of these features, preserve them if harmless, but do not expand them.

---

## 32. Rules for Codex

1. Work autonomously on environment/tool installation.
2. Inspect upstream before modifying.
3. Preserve working recording logic.
4. Use pinned Arduino-ESP32 3.x compatible PlatformIO tooling.
5. Do not downgrade core solely for WireGuard.
6. Establish the minimal feature profile before adding WG.
7. Plan flash/OTA partitions before UI growth.
8. Do not claim WG full tunnel without route verification.
9. Do not silently ignore unsupported WG directives.
10. Do not expose secrets.
11. Do not enable irreversible ESP security/eFuse settings automatically.
12. Keep recovery access available.
13. Prefer small commits.
14. When hardware is connected, compile + flash + verify.
15. If hardware is absent, complete host-side work and list exact hardware tests pending.
16. Adapt upstream architecture before refactoring it.
17. Keep `docs/PROGRESS.md` updated.
18. Document deviations in `docs/DECISIONS.md`.
19. Report firmware size after major phases.
20. Preserve recording integrity over UI smoothness.
21. Do not start with UI redesign.

---

## 33. Reference Sources

Primary base:

- `https://github.com/s60sc/ESP32-CAM_MJPEG2SD`

WireGuard:

- `https://github.com/ciniml/WireGuard-ESP32-Arduino`
- `https://github.com/ciniml/WireGuard-ESP32-Arduino/pull/52`

PlatformIO / pioarduino:

- inspect the current pioarduino PlatformIO-compatible ESP32 platform and pin a known-good stable release/commit after verification.

Board reference:

- `https://docs.platformio.org/en/latest/boards/espressif32/seeed_xiao_esp32s3.html`

Preferences/NVS:

- `https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/preferences.html`

Seeed XIAO ESP32S3 Sense:

- `https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/`
- `https://wiki.seeedstudio.com/xiao_esp32s3_camera_usage/`
- `https://wiki.seeedstudio.com/xiao_esp32s3_sense_mic/`
- `https://wiki.seeedstudio.com/xiao_esp32s3_sense_filesystem/`

Temperature sensor:

- `https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/temp_sensor.html`

Always inspect actual pinned source/version instead of assuming README examples match the chosen dependency.

---

## 34. Start Here

Codex should begin with:

```text
1. Check/install Git, Python, PlatformIO.
2. Clone ESP32-CAM_MJPEG2SD.
3. Record upstream SHA.
4. Identify a known-good pioarduino/Arduino-ESP32 3.x PlatformIO setup.
5. Build upstream with minimal source changes.
6. Pin the exact platform version/commit.
7. If hardware is connected: flash and validate camera/SD/mic/recording/OTA.
8. Create a minimal feature profile and revalidate.
9. Define an 8 MB dual-slot OTA partition plan and record size budget.
10. Implement versioned NVS config with active/pending rollback.
11. Create isolated WireGuard compatibility spike.
12. Implement WG-A remote access only after the spike is understood.
13. Add authenticated setup/rescue.
14. Finalize manual photo/video/audio/media flow.
15. Redesign the UI.
16. Implement and verify WG-B full tunnel last.
17. Run reliability and OTA tests.
18. Keep docs/PROGRESS.md current throughout.
```

Do not start with the UI.

Do not start with full tunnel.

First establish a known-good camera baseline, a minimal feature profile, OTA headroom, and a verified WireGuard backend.
