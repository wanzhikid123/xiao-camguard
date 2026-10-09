# Appliance HTTP integration

Updated 2026-10-04. Development integration, not the completed v2 appliance.

`xiao-appliance` extends the verified minimal recorder. It embeds the Chinese
login/dashboard HTML, CSS and JavaScript (including Chinese/English/German) from `ui/appliance` in firmware;
`scripts/embed_ui.py` generates the build-local header. No CDN or SD assets are
needed to render those pages. Real headless Chrome visual/interaction QA now
passes on the board at phone/tablet/desktop widths; see [BROWSER_QA.md](BROWSER_QA.md).
It uses installed Chrome directly after the native connector failed to start.

The profile now uses a single owning `NetworkService` task for versioned
`camcfg`, initial enrollment, Wi-Fi trials and reconnect/rescue. HTTP sends
bounded commands and reads masked snapshots. Wi-Fi consumes the Config's
complete 32-byte SSID directly; the legacy Wi-Fi globals no longer control the
product profile. Account creation writes a pending salted verifier and promotes
it only after a real candidate-LAN confirmation. An absent/unreadable active
account fails closed for camera/control/status APIs. See [setup](SETUP.md).
The recovery AP closes after the selected Wi-Fi is stable. Authenticated LAN
OTA and real failed-boot-health rollback now pass. The first integrated WG-A
gate also passes, including lifecycle/fault/UI checks; the owner confirmed
remote login/photo/recording and authenticated WG OTA passes. The separate
30-minute idle and whole 47.7 MB WG transfer checks now pass too.

## Registered routes

| Method / URI | Requirement / behavior |
| --- | --- |
| GET `/`, `/app.js`, `/i18n.js`, `/style.css` | Public embedded assets; CSP and no-store |
| GET `/api/bootstrap` | Public `adminConfigured`; initial setup nonce only on the actual AP socket when unconfigured/writable |
| POST `/api/auth/login` | Origin/request marker, bounded JSON; async password work |
| GET `/api/auth/result` | Random login-ticket header |
| GET `/api/auth/session` | Session; restores CSRF metadata |
| POST `/api/auth/logout` | Session, CSRF and same origin; revokes this client |
| GET `/api/account` | Session; public password policy and pending flag only |
| POST `/api/account/password` | Session/CSRF/origin; exact bounded current/new/confirmation JSON; shared asynchronous password worker, verifier-only owner commit, revokes all sessions on success |
| GET `/api/account/password/result` | Same origin/request marker and expiring private result-ticket header; single-use result survives old-cookie revocation, no other authority |
| GET `/api/status` | Session; explicit primitive allowlist, including `timeSynchronized`, POSIX `timezone`, `localTime`, UTC `epochSeconds`, real `audioInputRunning`, checked `cameraSensorSleeping`, `lastWakeFrameMs`, CPU/heap/PSRAM and capture-idle measurements; no Config serialization |
| GET `/api/camera/still` | Session; existing capture buffer returns JPEG |
| GET `/api/camera/stream` | Session; existing MJPEG task; no query/HEAD controls |
| POST `/api/record/start`, `/api/record/stop` | Session, CSRF, same origin, empty body, no query |
| POST `/api/events/start`, `/api/events/stop` | Session/CSRF/origin, no query; temporary 640×480 event-session monitoring; start accepts an optional sensitivity integer 1–10, stop requires empty body |
| POST `/api/device/restart` | Session/CSRF, empty body/no query; rejects recording and file finalization |
| POST `/api/setup` | Unconfigured account, actual AP destination, random setup nonce/request marker/origin; exact bounded Wi-Fi/admin JSON |
| GET `/api/setup/result` | Random expiring setup ticket/request marker; masked progress/IP/revision |
| POST `/api/setup/confirm` | Setup ticket/request marker/origin; actual candidate LAN socket and matching revision |
| GET `/api/network` | Session; masked network state/revision/IP |
| POST `/api/network/trial` | Session/CSRF/origin; exact Wi-Fi JSON, queued trial |
| POST `/api/network/confirm` | Session/CSRF/origin; actual candidate LAN socket/revision |
| POST `/api/network/cancel` | Session/CSRF/origin; empty body/no query; queued rollback |
| GET `/api/wireguard` | Session; masked interface/session/counter/error/import/probe primitives |
| POST `/api/wireguard/import` | Session/CSRF/origin; bounded plain-text single-peer config; owner validation and pending trial |
| POST `/api/wireguard/disable` | Session/CSRF/origin; empty body/no query; queued trial, local confirmation required |
| POST `/api/wireguard/clear` | Session/CSRF/origin; empty body/no query; candidate without WG keys, local confirmation required |
| POST `/api/wireguard/restart` | Session/CSRF/origin; empty body/no query; owner reconnects WG without NVS/Wi-Fi edits; unavailable during trial |
| POST `/api/wireguard/probe` | Session/CSRF/origin; one IPv4 target on selected WG routes; three unbound echoes, no NVS writes |
| POST `/api/camera/photo` | Session/CSRF/origin; empty body/no query; saves a new JPEG without overwriting |
| GET `/api/media/dates`, `/api/media` | Session; bounded date/file paging |
| GET `/api/media/file`, `/api/media/download` | Session; constrained path; async transfer, revocation checked per chunk |
| POST `/api/media/delete` | Session/CSRF/origin; exact media path; recording/finalization/download guards |
| POST `/api/media/delete-batch` | Session/CSRF/origin; exact paths array of 1–20 distinct application media files; validates all members before deleting, reports completed count; monitoring/recording/transfer guards |
| POST `/api/media/rename` | Session/CSRF/origin; exact path/name JSON, safe UTF-8 label, no overwrite, date/extension preserved; recording and in-use transfer guards |
| GET `/api/settings/capture` | Session; primitive capture values/defaults, revision and actual sensor/resolution capabilities |
| POST `/api/settings/capture` | Session/CSRF/origin; bounded partial integer values, atomic validation, capture-owner application and dual-bank NVS commit; rejects recording/finalization |
| GET `/api/ota` | Session; progress/error and actual slot/budget primitives |
| POST `/api/ota` | Session/CSRF/origin; bounded raw application image and required SHA-256; async receive, maintenance/revocation, dual-slot validation |

`cameraSensorSleeping` reflects successful OV3660 power-down register readback
by the capture owner. `lastWakeFrameMs` is its last confirmed fresh wake frame's
monotonic start time in milliseconds; it is zero before the first such wake and
wraps like the device clock. No register read/write endpoint is exposed.

The new profile does not register the old `/control`, `/web`, `/status`,
`/update`, `/upload`, `/wifi`, `/sustain`, `/ws` or `/sse`. Their mixed commands,
raw settings and long-lived commands cannot bypass the new checks. Baseline
environments keep their original interfaces, including original OTA.

The media workflow passes 44 real-account hardware checks and independent
complete JPEG/AVI decoding; see [MEDIA.md](MEDIA.md). The server reserves sixteen
response-header slots for security headers plus cookies/file disposition.
Authenticated LAN OTA passes 64 checks including a boot-health rejection
fixture that actually returns to the previous valid slot; see [OTA.md](OTA.md).
WG-A's first 33-check gate covers real session/data, LAN coexistence and reset
recovery; see [WIREGUARD_REMOTE_ACCESS.md](WIREGUARD_REMOTE_ACCESS.md).

The stream task stores only its session token in RAM, checks it before/after
each frame wait, does not refresh the session's idle timer, and wipes it when
the async request completes. Logout therefore closes an existing stream rather
than protecting only the next connection. Frame-wait and socket-send bounds
still apply. Sessions remain independent between clients.

Video choices are QVGA 320×240 and VGA 640×480; requests above VGA are rejected.
A valid older saved video resolution above VGA is clamped to VGA during boot
without discarding the remaining capture settings.
Current capture defaults are video VGA 640×480, requested 15 FPS, photo
2048×1536 and fourfold (+12 dB) microphone gain, 16 kHz/16-bit/mono PCM.
Apply saved capture values after the legacy reload so its frame-size side
effects cannot overwrite the FPS. See [capture controls](PRODUCT_REFINEMENTS.md).
The original capture/SD/audio/AVI workers remain in place. HTTP only requests
recording; the capture task owns file creation and finalization. Status remains
`recording=true` until finalization finishes, and restart/start are refused
during that interval.

The development profile suppresses the upstream gateway-ping owner (which can
restart networking and reconfigure both idle watchdogs). The pinned SDK's
initial watchdog policy remains unchanged. The product network service replaces
that owner; the explicit RAM authentication fixture retains its isolated AP.

## Actual hardware gate

`xiao-appliance-auth-test` is a separate, explicit build with a known-board MAC
check and a synthetic RAM verifier. It neither opens nor writes `camcfg` and
uses `XIAO-Camera-Test-084C`. Never deploy this fixture to users.

67 real checks passed, including denied anonymous/Basic requests, removed
legacy routes, no GET mutations, CSRF/Origin enforcement, authenticated JPEG,
15-FPS/audio defaults, recording during KDF, restart refusal during recording,
continued preview during KDF, logout closing the existing stream, and an
independent second client remaining authenticated. Nested/deep JSON and decoded
NUL are rejected before parsing; both extra and empty control query strings are
rejected without starting a recording.

- 622 complete preview frames during the bounded visit; 3 JPEG frames saved.
- Stream ended 0.094 seconds after logout.
- 198 HTTP/page/status probes: maximum 0.135 seconds; no connection retries.
- Login times: 34.975 seconds in recovery, 42.362 seconds with camera active,
  45.701 seconds with preview/recording running. This remains a slow login.
- Recording: 283 frames, requested 15 FPS / measured 13.9 FPS; actual PCM
  captured 326,144 samples. Both integrated-test AVIs were downloaded and
  decoded completely with FFmpeg. Their video/audio duration differences are
  16 ms and 8 ms; see [TEST_REPORT.md](TEST_REPORT.md).
- Fixture firmware: 1,825,856 bytes (54.63% of a 3,342,336-byte slot), static RAM
  100,824 bytes. The automatic 80% OTA budget gate passes.

The hardened non-fixture build also passes 33 actual checks with no active
account: embedded assets remain available, camera/status/control APIs remain
locked, login returns `admin_not_configured`, nested JSON is rejected before
parsing, and ordinary brackets or a literal backslash-unicode sequence inside a
password string remain valid JSON. No account was created. This build uses
1,830,560 bytes (54.77%), static RAM
100,920 bytes. Ignored report: `.tools/hardware/appliance-locked/report.json`.

Ignored evidence: `.tools/hardware/appliance-auth/report.json`,
`appliance-auth-boot.log`, `appliance-auth/still.jpg`, `appliance-auth/preview-*.jpg`.

```powershell
python scripts/pio.py run -e xiao-appliance
python scripts/pio.py run -e xiao-appliance-auth-test -t upload --upload-port COM3
.tools/platformio/penv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_auth.py --restore-profile Ouyuan5G
python scripts/pio.py run -e xiao-minimal -t upload --upload-port COM3
```

The chosen admin password is now saved in the ignored private file and the
production account is enrolled; normal-reset login and Wi-Fi recovery pass.
See [AUTHENTICATION.md](AUTHENTICATION.md) and [SETUP.md](SETUP.md).

## Event capture and batch deletion

`POST /api/events/start` accepts an empty body (default sensitivity 5) or
`{"sensitivity":5}` as the exact JSON schema; the integer range is 1–10.
`POST /api/events/stop` requires an empty body. Both require session, CSRF
and same Origin and return 202 when accepted. The capture owner applies a
temporary VGA with a 15 FPS monitoring target and 5 FPS timestamp recording
target; stored capture settings are restored on disarm.
Starting is blocked during manual recording, playback, capture changes,
downloads, unavailable camera/microphone/SD or insufficient free space.
Manual recording, saved photos, sensor setting changes, restart and OTA are
blocked while event monitoring is requested/armed. Preview/status remain usable.
Stopping finalizes one video containing every motion event in the monitoring
session and restores the stored video settings. Quiet gaps are omitted; empty
sessions create no file. Each saved JPEG visibly shows its actual capture
clock at the top right in the AVI itself. Idle MP4 conversion only transcodes
those pixels; it does not add or repaint the time.

`GET /api/status` adds `events`: requested/armed/quietPending flags, sensitivity,
changedPermille, savedClips (finalized session videos since boot), sessionEvents
(events in the latest monitoring session), last decodeMs and a bounded error code.
Boot is disarmed; logout does not disarm. See [EVENT_CAPTURE.md](EVENT_CAPTURE.md).

`POST /api/media/delete-batch` accepts only `{"paths":["/YYYYMMDD/...jpg"]}`:
1–20 distinct application media paths, JSON body at most 3,200 bytes, session,
CSRF and same Origin. Queries/escapes/duplicates/extra keys/nonmedia paths are
refused. Every file's existence is checked before the first removal. Recording,
armed monitoring and any active media transfer refuse the entire batch. The
response has `deleted` and `error`; a storage failure may report partial deletion.
The UI's delete-all enumerates all date/file pages before confirmation, then
uses this bounded API. There is no unrestricted recursive SD deletion endpoint.

Only the explicit event fixtures (`xiao-appliance-event-test` and the combined
`xiao-appliance-event-codec-test`) add authenticated LAN `POST /api/events/test`
with a RAM-only scene fixture after actual JPEG decoding. Normal images have
no such route. It does not prove physical motion sensitivity.

## IPv4 full-mode API

`POST /api/wireguard/mode` accepts the flat one-field object
`{"mode":"full_tunnel"}` or `{"mode":"remote_access"}`. It requires session,
CSRF, same Origin and the actual LAN socket. Disabled WG, an unchanged mode,
another pending operation or an existing trial is refused. It queues the
existing network owner's mode-only configuration trial; use the existing
`/api/network`, `/confirm` and `/cancel` workflow. Full-mode confirmation also
requires real current public-route proof. It cannot be confirmed through WG.

`GET /api/wireguard` now includes `mode`, `fullTunnelVerified`, `guardReady`,
`routeProbeBusy`, `publicExit`, `verificationAgeSeconds`, `routeProbeHTTP`,
`ipv4Blocked`, `ipv6Blocked`, `bootstrapDNSPackets` and `bootstrapTimePackets`.
These are observations; `enabled`/`running` alone does not verify the exit.
The historical imported-default-route exclusion flag is false in full mode.
No key, Wi-Fi password, verifier, salt or configuration export is added.

The `xiao-appliance-full-policy-test` environment alone adds the authenticated
`/api/wireguard/policy-test` fixture. It injects a cold clock, synthetic packet
matrix, interface hold, disconnected Wi-Fi and documentation-only bad endpoint.
Its hostname alias is supplied in RAM. Never ship this image; normal builds
have neither this API nor the old RAM-spike API.
