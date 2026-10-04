# Photos and authenticated media

Updated 2026-10-03. Implemented in `xiao-appliance`; the 44-check hardware gate passes.
The original camera/SD/AVI/PDM workers remain in place. The new APIs do not
register or call the upstream generic control, file upload or path parser.

`POST /api/camera/photo` checks session, CSRF, origin and an empty body/no query.
The capture task temporarily applies the selected photo resolution/quality,
then restores video settings. It uses the existing kept-frame buffer, writes the JPEG to SD in bounded SRAM
chunks, and returns only saved path/size/time-readiness. Names use one capture
timestamp and a random suffix; existing files are never overwritten. Partial
write cleanup removes only this request's new file. Missing/unwritable storage
returns an error and never formats the card.

The media browser supports date folders, descending filenames, pages of twenty,
single JPEG preview, downloads and explicit confirmed deletion. AVI is MJPEG
with PCM audio; the browser offers download rather than claiming universal
native AVI playback. Date/file enumeration retains at most 21 entries and scans
at most 4,096 entries or two seconds per request. `enumerationTruncated` is
reported explicitly if that bound is reached; such a page is not a complete
inventory. The cursor is the last filename/date, exclusive on the next page.

| Route | Contract |
| --- | --- |
| GET `/api/media/dates[?before=YYYYMMDD]` | Session; bounded date page |
| GET `/api/media?date=YYYYMMDD[&before=filename]` | Session; bounded file page |
| GET `/api/media/file?path=...` | Session; JPEG inline, AVI attachment |
| GET `/api/media/download?path=...` | Session; attachment |
| POST `/api/media/delete` | Session/CSRF/origin; exact one-field JSON `path` |
| POST `/api/media/rename` | Session/CSRF/origin; exact `path` and UTF-8 `name`; date prefix/extension preserved |

Paths allow only `/YYYYMMDD/YYYYMMDD_HHMMSS_metadata.jpg` or `.avi`.
Metadata is a bounded validated UTF-8 label; ASCII permits letters/digits/space/
underscore/hyphen, with malformed UTF-8, control/bidi characters and unsafe
punctuation refused. No nested paths,
dot segments, configuration files, alternate extensions, NUL, double-decoded
paths, duplicate or unknown query fields are accepted. No directory deletion.
Deletion is refused while recording/finalizing and for the current download
target. AVI downloads are refused during recording to avoid serving an open
recording; photo access and browsing remain available.
Rename labels are at most 64 UTF-8 bytes, have no leading/trailing spaces and
retain `/YYYYMMDD/YYYYMMDD_HHMMSS_` plus the original extension. An existing
different target is never overwritten. Recording/finalization and an in-use
source or target download return 409. Files get an RFC 5987 UTF-8 attachment
name; path query values must be percent-encoded.

One asynchronous download task owns its File and 4 KiB SRAM buffer. It does not
occupy the HTTP request task for the whole file and never uses the recorder's
shared SD buffer. It checks the session before every chunk, caps total transfer
time at thirty minutes, uses the existing socket send timeout, closes the File and
wipes its token on completion. Logout can revoke an already-running download.
Cookies are HttpOnly/SameSite and the response is no-store, without CORS.
The old ten-minute deadline interrupted a 47.7 MB WG download at 28 MB. The
separate retry now passes 26 checks: all 47,667,154 bytes arrive in 533.55 seconds,
SHA-256 matches the existing local reference, and complete MJPEG/PCM decoding
passes. There was no reset and status stayed reachable. The earlier failure is
retained; the latest network path was faster as well as having a longer deadline,
so this does not establish identical performance under the old slow conditions.
Evidence: `.tools/hardware/appliance-large-wg/report.json`. Reserve checks
return 507 before a new photo/recording when free space is below the selected
limit. A quota larger than the whole card passed refusal/preservation checks;
this does not establish real filesystem-full write failure behavior.

The later refinement gates pass actual photo dimensions through 2048×1536,
UTF-8 photo and video renames with byte-identical downloads, and in-use rename
denial. See [PRODUCT_REFINEMENTS.md](PRODUCT_REFINEMENTS.md).

Reproducible real-account gate:

```powershell
python scripts/pio.py run -e xiao-appliance -t upload --upload-port COM3
.tools/platformio/penv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_media.py --address 192.168.1.181
```

The harness creates 22 test photos to exercise paging and one manual audio AVI.
It deletes only its own generated files, retains one photo/video and compares
every pre-existing path/size. It checks traversal/config access, CSRF, recording
guards and an owned video download held with a small receive buffer, then logout
revocation. Evidence stays in ignored `.tools/hardware/appliance-media/`.
The earlier network-trial AVI is also downloaded for complete FFmpeg validation.
The final real-account run passes 44 checks. It preserves all ten original
files by path and size, retains one generated photo/video, and removes only
its own other photos. Logout closes the held download after 77,824 bytes in
0.015 seconds; status remains responsive during that transfer. Initial boot
recovery used the normal authenticated restart, retaining the RTC crash guard.

Saved and downloaded JPEGs have identical SHA-256 and decode completely. Both
downloaded AVIs pass full FFmpeg `-xerror` audio/video decoding and ffprobe's
MJPEG/16 kHz mono PCM contract. The new manual recording has a 9 ms audio/video
duration difference; the earlier network-trial recording has a 58 ms difference.
Evidence: `report.json`, `decode-report.json` and `appliance-media-final-boot.log`
under ignored `.tools/hardware/`. Subjective speech quality remains unverified.

The first download run exposed a missing Content-Disposition: adding the same
security headers in both the handler and async worker exhausted the SDK's
eight response-header slots. The worker now relies on the copied headers and
the product server reserves sixteen slots. The pinned SDK appends headers and
copies them when beginning an async request; see the primary
[ESP-IDF v5.5.5 implementation](https://raw.githubusercontent.com/espressif/esp-idf/v5.5.5/components/esp_http_server/src/httpd_txrx.c).
The complete gate was rerun after this fix. The verified production image is
1,763,760 bytes (52.77% of one slot), static RAM 105,524 bytes, SHA-256
`26b427740f0f39f3d6f208ecc5df689085ac3d302adf75e95364f16cc2fe2e73`.
