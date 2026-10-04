# Actual offline UI gate

## BLE, photo folders and MP4

The current feature gate uses actual authenticated Chrome on the board, checks
the four installed assets, CN/EN/DE at 390/768/1366 px, and real interval-photo
start/stop, settings locks and cancel/confirm deletion. PIN responses are held
in RAM across logout; after clearing a previously displayed PIN, a language
change must also leave it blank. Screenshots are taken before PIN display.
The separate MP4 gate exercises native H.264/AAC playback, time progress, seek
and authenticated Range 206 on a published, explicitly owned clip.
The latest feature fixture passes 29 UI checks and five native MP4 checks;
Chinese/German mobile screenshots were visually inspected. There are zero
JavaScript errors and only the language code is persisted in browser storage.
Final results are in [IMPLEMENTATION_SUMMARY](../IMPLEMENTATION_SUMMARY.md).
These Chrome checks do not establish physical iPhone Safari support.

## Event mode and media selection — previous normal image

The final 1,916,528-byte event image passes **80 real Chrome/HTTP/media checks** in
`.tools/hardware/event-media-browser-final/report.json`. All four embedded assets
match source, and disarmed status already reports the restored resolution. Tests exercise
the actual event start/stop controls and protect deletion while monitoring or
recording. Chinese, English and German each pass 390/768/1366 px layouts;
Chinese and German mobile media rows were visually inspected. There are zero
uncaught JavaScript errors and web storage contains only the language code.

Selection spans 24 actual owned files across two pages, including a Unicode
name. Cancelling the confirmation deletes nothing. Delete-selected removes
two owned files; delete-all removes the remaining 22 in bounded 20+2 batches.
All 133 entries present at the start of this gate and all 31 settings remain
unchanged; 44 of these entries belong to earlier failed event fixtures and
have separate ownership/cleanup records. Existing user files are never used
as deletion targets. A separate mocked browser inventory covers 41 files in
21 dates and multiple file pages, enumerating everything before 20+20+1
batches. An incomplete scan makes zero deletion requests. Mocked pagination
does not establish additional real SD deletion coverage.

Subsequent final reconciliation removes only those 44 proven failed-run files
and retains all 89 originals plus the three passing event clips. Additional
390 px screenshots capture the actual media toolbar in all three languages;
Chinese and German are visually inspected, including both deletion buttons,
selection counts and the real retained event rows. Evidence is separate in
`events-final-visual.json` and `event-media-final-toolbar-{cn,en,de}.png`.

The same gate fully decodes a real 6.323-second manual MJPEG/PDM recording
(PCM 6.336 seconds, 13 ms difference), runs the production JPEG detector without
errors, and ends disarmed with camera/SD and verified full tunnel healthy.
The preceding 77-check pass and earlier CSP polling/install-order failures remain archived; the browser
harness now uses locator assertions, and the firmware CSP remains unchanged.
Physical phone/Safari and object-motion sensitivity remain separate gates.

Updated 2026-10-04. Headless installed Chrome 154.0.8037.95 and isolated
Playwright Python 1.63.0 successfully exercise the real production board at
`192.168.1.181`. This uses the browser executable directly; it does not depend
on the previously unavailable native browser connector. No user browser
profile is modified. No traces, HAR, cookie/CSRF dumps or credential logs.

The gate tests anonymous/login visibility, labels, masked/cleared password,
duplicate-login prevention, actual chosen-password login, status/media loading,
live MJPEG and still/JPEG decoding in Chrome, photo save/list/preview/download,
confirmed deletion of only that run's photo, manual audio recording controls,
finalization, Wi-Fi/OTA setting expansion, session refresh and logout cleanup.
There are no uncaught JavaScript errors and no local/session storage entries.

Screenshots cover 1366 px desktop, 390 px phone and 768 px tablet widths with
no horizontal page overflow. Login, dashboard, preview and expanded settings
screenshots were produced; desktop/mobile dashboard screenshots were visually
inspected. The actual camera scene is dark, but the JPEG dimensions and decoder
checks pass. This does not establish physical phone browser behavior, speech
quality, or visual acceptance of future WireGuard/settings additions.

The separate final WireGuard UI gate now passes 34 checks: actual private file
and field import, masked/cleared keys, real peer state, LAN confirmation,
restart, clear/cancel restoration, logout and empty web storage. Expanded
fields have no horizontal overflow at 390/768/1366 px. Screenshots were taken
only after private controls were empty and were visually inspected. Ignored
evidence: `.tools/hardware/browser-wireguard-final/report.json` and PNGs.
The light UI/capture refinement gate additionally passes **46 actual Chrome
checks through 10.8.0.5**, on the deployed 1,882,144-byte image. Chinese, English
and German each pass 390/768/1366 px layout checks with no horizontal overflow
and zero uncaught JavaScript errors. All 31 capture fields, CPU/RAM percentages,
RSSI descriptions, three supported CPU clocks and audio gain labels load.
Actual UI save/restore, photo save, rename and 2048×1536 preview pass. German
survives reload and switches to English, including toggle options. Web storage
contains only the language code; password is cleared before verification and
screenshots. The real Chinese desktop screenshot was visually inspected.

The first reload assertion mistakenly read `innerText` inside a collapsed
`details`; actual `textContent` and saved language were correct. Opening the
panel before checking fixes the test, without changing or weakening firmware.
Ignored evidence: `browser-refinements/report.json`, nine PNGs and
`first-german-reload-report.json`. These checks do not establish physical iPhone
Safari behavior or subjective approval of the design.

The microphone stop/start image subsequently passes **50 checks** through WG,
adding the clock-aware login hint and correct Chinese/English/German labels for
mic off (preview/photos remain available). Evidence is separate in
`browser-microphone-final/report.json` and its nine PNGs. This precedes the
later low-clock ticket-deadline fix; that fix has its own actual 80 MHz gate.

The normal recovery image passes **27 actual Chrome checks over LAN** at real
80 MHz after camera/PDM standby. The mobile 390×844 page authenticates in
101.63 seconds, loads all 31 fields, wakes and decodes a 640×480 still, retains
the selected clock, has no overflow/JS error, stores only the language code,
logs out and restores the original values/240 MHz using the independent RAM
session. Three status transactions during password work take at most 156 ms.
The screenshot is visually inspected. This differs from the earlier 168-second
WG diagnostic workload. Firmware assets match all four source files after
authenticated OTA; the normal image excludes the WG TCP fixture. Evidence:
`low-clock-browser-final/report.json`, `80mhz-mobile.png` and
`low-clock-final-serial.log`. Prior CSP-polling/PC-VPN/camera failures remain
separate; the product CSP was not relaxed to make the harness pass.

The sensor-power-down normal image later passes **25 actual mobile Chrome
checks** over LAN: real 80 MHz sensor/PDM standby, 101.53-second login, all 31
fields, checked sensor wake with a fresh frame timestamp, decoded 640×480
still, and visible status/buttons following the actual wake. Three status
transactions during password work take at most 177 ms. No JavaScript error,
overflow or credential storage is observed; original values/240 MHz are
restored. Evidence: `low-clock-browser-sensor-final/report.json`, full-page
`80mhz-mobile.png`, visually inspected `80mhz-mobile-viewport.png`, and
`sensor-browser-wake-serial.log`. No additional OTA is performed in this gate;
the same image has the independent 102/101/44 hardware reports.

```powershell
python -m venv .tools/browser/venv
.tools/browser/venv/Scripts/python.exe -m pip install playwright==1.63.0
.tools/browser/venv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_browser.py --address 192.168.1.181
```

Administrator password rotation passes **85 account/hardware checks**, including
an actual Chrome success, all-session revocation, 240/80 MHz changes, persistence
and restoring the chosen password. The final message/image gate separately
passes **42 checks** on the same backend: account titles and live errors in all
three languages, nine width/language layouts, local mismatch/policy rejection
without a request, one actual wrong-current proof, cleared inputs, restored
button/session, exact final assets and no private browser storage/JS errors.
Final empty-input account screenshots are visually inspected (including the
complete German form). Evidence: `account-candidate/report.json`,
`account-final/report.json` and `account-final/account-{cn,en,de}.png`.
No trace, HAR, request bodies, cookies or tickets are recorded. A synthetic
temporary recovery password is retained only in the ignored private directory,
audited separately, and is no longer selected on the board.

```powershell
.tools/browser/venv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_account.py --address 192.168.1.181 --firmware .tools/hardware/appliance-account-candidate.bin --out .tools/hardware/account-candidate
.tools/browser/venv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_account_browser.py --address 192.168.1.181 --firmware .tools/hardware/appliance-account-final.bin --out .tools/hardware/account-final
```

Use fresh output directories for repeats; the archived candidate and final
images differ only in the unconfirmed-result wording. Rotation retains a private
recovery value in case execution is interrupted after commit, then restores the
chosen local-file password and original capture settings before success.

The final WG-B ordinary image passes **42 actual Chrome/media checks**. All
four assets match source; both hardware fixture endpoints return 404. The
three mode labels, actual verified-exit state, preserved unsubmitted selection
and nine 390/768/1366 px layouts pass, with zero JavaScript errors/overflow and
no private web storage. Chrome really submits and confirms the full-mode trial.
The initial run found its confirmation button inside folded Wi-Fi settings;
the fixed UI opens that panel once for each new revision. Its failed 23-check
report is retained separately in `full-final-browser/`, while the corrected
report and screenshots are in `full-final-browser-ui/`.

Chinese and German screenshots of the actual verified full mode are visually
inspected; native file-picker labels follow the Chrome/OS locale. All private
inputs are empty before screenshots; no trace/HAR/cookie dump is recorded.
New and saved JPEGs fully decode, and a new 1,461,338-byte PDM AVI downloads
whole and fully decodes (8.133/8.128 s, 5 ms). All 82 earlier media entries,
31 capture values and actual CPU 240 remain. The gate leaves confirmed full
mode at revision 71 on `appliance-full-ui-final.bin`, **1,894,496 bytes**.

```powershell
.tools/browser/venv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_full_browser.py --address 192.168.1.181 --firmware .tools/hardware/appliance-full-ui-final.bin --out .tools/hardware/full-browser-repeat
```

Run this mutation gate alone; it retains its new photo/video and does not
delete existing media. Final-image remote-peer and physical phone behavior
are separate from this LAN Chrome gate.

Ignored evidence: `.tools/hardware/browser/report.json`, `*.png` and
`ui-photo-download.jpg` (complete FFmpeg decoding also passes). The created
photo was explicitly deleted through the UI; the newly recorded video remains
on SD. Test failures print only their stage/type, because Playwright call logs
can contain password input values. Password file: ignored local single-line
UTF-8, read into RAM only.
