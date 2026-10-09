# 480p event-session video

Current behavior (2026-10-09): one 640 × 480 video per monitoring session,
containing all motion events between start and stop. Manual video is limited
to 640 × 480; photo resolution remains independent. The acceptance records
below dated 2026-10-04 describe the earlier separate-clip implementation.
Current reproduction and acceptance: [EVENT_AVI_CLOCK.md](EVENT_AVI_CLOCK.md).
Initial idle failure: [EVENT_SESSION_FIX.md](EVENT_SESSION_FIX.md).

## Event lifecycle

- Arm explicitly with sensitivity 1–10 (default 5); reboot starts disarmed.
- Temporarily use VGA at a target 15 FPS while monitoring and 5 FPS while
  saving timestamped event frames; retain the user's stored capture
  settings and restore them when disarmed. Parameters and saved photos are
  blocked while armed; preview and status remain available. Mode guards remain
  active until sensor resolution/FPS restoration finishes.
- Decode one eighth size JPEGs at most four times per second, compare 80 × 60
  grayscale frames, compensate uniform brightness shifts, and require two
  consecutive changes to start an event. Allow two seconds for initial settling.
- Save ordinary MJPEG video and 16 kHz mono PCM only during an event.
  Hold candidate quiet video and matching audio in bounded PSRAM buffers.
  If motion resumes within one second, append both buffers; otherwise discard
  that quiet tail, pause audio/video writing, and continue monitoring into the
  same file. Detection is gated at 250 ms; processing can make it slower.
  Stop monitoring to finalize
  the session; a session with no motion creates no video.
- Keep brief events instead of applying the manual recorder's five-second
  minimum. Use random suffixes to avoid same-second naming collisions.
- Spool the session AVI index to SD instead of imposing the old 4,000-frame
  split or retaining a growing RAM index. Stop and finalize the single file
  at the AVI 1.0 32-bit size/timing boundary; never silently split it. Stop
  safely on storage, audio or repeated decode failures. A full tail buffer
  ends the current event at the quiet boundary and resumes monitoring.
- Before writing each MJPEG chunk, decode the JPEG, paint its actual capture
  date/time at the top right (white on black), and encode it again. AVI already
  displays the clock, even with conversion disabled. Held frames use their
  original capture time. Version-2 APP15 metadata retains that time for inspection.
  MP4 conversion only transcodes these existing pixels; it does not draw time.
  Removed quiet gaps appear as wall-clock jumps. The clock uses the device
  timezone, as existing filenames do.
- Arming prevents unattended sensor sleep. Logging out leaves monitoring on;
  disarm explicitly to return to normal idle saving. This saves storage rather
  than providing deep sleep. No person recognition or pre-roll is included.

Detector images and the bounded video/audio tail use approximately 600 KiB of
PSRAM in addition to the camera. The event index uses a hidden temporary SD
file which is removed on finalization. Detector/tail allocations are
released when disarmed. Sampling uses actual JPEG decode even in the integration
fixture, which then substitutes only its small grayscale detector input.
Clock rendering owns full-frame JPEG buffers and codec handles, allocated once
per monitoring session and released before idle conversion. The pinned JPEG
component's private workspaces use PSRAM, preserving internal RAM for the live
camera, PDM and TCP responses. Generic SDK allocation policy is unchanged.
Event recording targets 5 FPS; actual rate includes the JPEG repaint cost.
Monitoring returns to a 15 FPS target after each event.

There is no fixed five-minute or other minute-based event limit. The session
stops at the AVI 1.0 approximately 4 GiB size/timing boundary, 4,294,967 frames,
or storage/error protection. PCM-containing files normally reach the size
boundary first; a frame-limit calculation is not a recording-time promise.
The web download handler independently supports files up to 2 GiB minus one
byte. Long sessions beyond that size are not established as downloadable.

## Bulk deletion

Every row has a checkbox; selection spans pages of the currently selected date
and resets when the date/list is refreshed. Select-all applies to loaded rows.
Delete-selected and delete-all require a confirmation naming their scope.
Delete-all first enumerates every date and every page and aborts if the scan is
incomplete. It deletes the resulting snapshot in batches of at most 20 files.
Only application JPEG/AVI paths are accepted; configuration, temporary files,
directories and unrelated SD files are never deleted. The server checks session,
CSRF, Origin, mode, transfer activity, schema, duplicates and existence before
each batch. A failed SD write/deletion may leave partial results; the UI reports
the completed count and refreshes the list. There is no automatic media purge.

## Acceptance — 2026-10-04

Both pinned builds pass. The final normal image is **1,916,528 bytes / 57.34%**
of the OTA slot, static RAM **103,644**. The fixture is **1,917,760 bytes /
57.38%**, static RAM **103,652**. Private BIN/ELF/digests are named
`appliance-events-final` and `appliance-events-final-fixture`; the normal digest
is `18b9fb47376bca344a0de857c89ab0227435efc692c646914315e26d3ba67682`.

The **58-check real-board integration gate** passes in
`.tools/hardware/events-fixture-final/report.json`. It uses RAM-only detector
input substitutions after actual JPEG decode, with real VGA frames, PDM audio,
SD files, authenticated HTTP and normal OTA restoration. Six detector algorithm
cases pass on the board. Static input for 35 seconds, noise and uniform lighting
for five seconds each create no clips; monitoring also survives logout and
overrides a temporary 30-second idle policy. The 119 monitoring observations
have a maximum JPEG decode duration of 31 ms; free heap ranges 83,124–140,044
bytes and minimum free PSRAM is 3,075,760 bytes. These are observations in this
workload, not a sustained-performance or power measurement.

Continuation across a short quiet gap, a brief event and explicit disarm create
exactly three separately named 640×480 MJPEG/16 kHz mono PCM AVI files. All
download and decode completely with FFmpeg `-xerror`:

| Case | Video seconds | PCM seconds | Difference |
| --- | ---: | ---: | ---: |
| Motion / short quiet / resumed motion | 6.584 | 6.592 | 8 ms |
| Brief event | 1.109 | 1.088 | 21 ms |
| Disarm during event | 1.295 | 1.280 | 15 ms |

Sensor restoration reports the original 1600-pixel video width before disarmed
status, all 31 stored values remain unchanged, and the restored JPEG decodes.
The final normal OTA excludes the fixture endpoint, starts disarmed and retains
the confirmed full tunnel with verified expected server exit. This is an
outbound proof, not fresh remote-peer inbound acceptance.

The **80-check production Chrome/HTTP/media gate** passes on the exact normal
digest in `event-media-browser-final/report.json`: four source-matching assets,
real detector execution, mode conflicts, strict batch/auth/existence guards,
actual owned-file deletion, three languages/nine layouts, cancelled confirmation,
selection across two pages, and complete all-date enumeration before deletion.
Twenty-four owned files are deleted through real SD endpoints. A separate
41-file/21-date browser model tests pagination without real deletion. Incomplete
enumeration initiates no deletion. A real manual audio AVI still fully decodes
(6.323/6.336 seconds, 13 ms difference). Nine host unit checks, Python/JavaScript
syntax and Git whitespace checks also pass. See [browser evidence](BROWSER_QA.md).

Only proven test files are deletion targets. Final reconciliation passes:
all **89 preexisting media paths and sizes** remain unchanged, 44 failed-run
owned files are removed in 20+20+4 batches, and the three passing event clips
are retained (92 files total). Every other path/size also remains unchanged.
The separate failed-run ownership ledger and cleanup reports are
`events-red-owned.json` and `events-red-cleanup.json`. Final mobile media toolbar
screenshots in all three languages pass; Chinese and German are visually
inspected, including selection counts and both deletion controls. Existing
user media must never be used to exercise a whole-card clear.

The final 600.1-second serial capture contains all three successful event closes
and normal firmware restoration/idle operation, with zero panic, watchdog,
stack-canary or heap-corruption matches. Evidence:
`events-final-green-serial.log` and `events-final-serial-summary.json`.
The exact staged snapshot and named firmware/report/log artifacts are checked
against all six supplied local private values with `scripts/audit_private.py`;
credentials, configuration exports and hardware evidence remain ignored.

Physical motion sensitivity and a real stationary-camera scene are **not
tested**: the user deferred arranging the camera and moving an object. Slow
motion, local lighting/shadows, speech quality, measured power, physical phone
browsers, long event splitting and actual storage failure remain distinct
acceptance items; synthetic input does not prove them.

## Retained failures and corrections

- The first harness expected HTTP 401 for unauthenticated mutation; the
  existing authorization contract correctly returns 403. A fixture LAN check
  then rejected IPv4-mapped IPv6 sockets; the corrected check handles both.
- A stale timestamp captured before opening the AVI underflowed unsigned clip
  duration and produced 41 one-frame 10–22 ms files. Refreshing time after open
  and decode fixes this; the gate explicitly checks sustained recording before
  the first save. These failed clips have serial/time-window and inventory proof.
- Candidate audio-tail discard could briefly resume WAV writes before close.
  Finalization now stops writes and discards the quiet tail atomically under
  the audio mutex; the passing short and continued clips include real PCM.
- The next integration correctly found disarmed status before sensor resolution
  restoration. Mode guards now remain active until restoration finishes. Its
  three owned clips decode separately in `events-restoration-red-media`, but
  that failed run is not counted as passing acceptance.
- CSP polling and a harness cleanup-before-install ordering failed separately.
  Locator assertions replace string polling; normal OTA precedes old owned-file
  cleanup. Firmware CSP and original-file ownership guards remain intact.

Failed reports and distinct candidate images remain private evidence, including
`events-fixture-{first,auth-fixed,lan-fixed,timing-fixed}` and
`event-media-browser-{first,auth-fixed,csp-fixed,install-order}`. Serial captures
remain separate from each report; no failure is overwritten by the green runs.

## Idle conversion regression — 2026-10-08

A live idle device reported three `h264_init_failed` failures and a disabled
conversion switch. The same source converted after reboot. A six-second normal
VGA recording then reproduced the initialization failure at frame zero, while
retaining roughly 372 KB of PSRAM. The completed AVI index accounted for
320,032 bytes; `buildAviHdr` now releases that index after it has been written.
The next recording allocates its own index again. The source AVI remains
protected by the existing transactional MP4 publication/validation rules.

The regression fixture `xiao-appliance-event-codec-test` combines detector
scene substitution with an exact-source conversion allowlist. It must never
ship. Run `experiments/hardware/test_event_session.py` with explicit fixture,
production restore and evidence paths; it preserves original media and removes
only its owned files. Live acceptance results are recorded separately.
