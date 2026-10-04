# Authentication and WireGuard import

`lib/appliance-config` now includes password verification, volatile sessions,
an asynchronous HTTP adapter and atomic single-peer WG text import. The pinned
Arduino 3.3.12 / IDF 5.5.5 build and hardware gates pass. The new root
`xiao-appliance` development profile now uses the module for camera, status,
recording and restart access. Initial enrollment and Wi-Fi repair now use the
single network owner and pass actual product checks. WG-A/media/OTA integrations
also pass their separate product gates; baseline environments keep their
original development interfaces.

## Passwords

Use SDK `mbedtls_pkcs5_pbkdf2_hmac_ext`, PBKDF2-HMAC-SHA256, 600,000 iterations,
16 random salt bytes and a 32-byte verifier, following the
[OWASP PBKDF2 work factor](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html).
The configuration schema 1 admin payload is unchanged. It never stores plaintext
admin passwords. NVS remains unencrypted storage; hashing does not encrypt
Wi-Fi/WG configuration or HTTP transport.

Require 10 or more Unicode characters, valid UTF-8 and at most 128 encoded
bytes; reject NUL/ASCII controls without trimming or truncation. Failed
enrollment preserves the old verifier. Comparison uses mbedTLS's constant-time
comparator; transient verifier/material is wiped. The caller must wipe its
plaintext input. Wi-Fi RF must be active when using `esp_fill_random` for
salt/session entropy.

CPU1 priority-one KDF work takes 34,099 ms. A priority-zero comparison took
68,426 ms; a portable-software probe took 97,911 ms and was removed. Login must
show a waiting state and run on the worker. Camera concurrency now passes:
42.362 s with camera active and 45.701 s with preview/recording. Integrated
WireGuard has separate passing product gates. No watchdog configuration changes; CPU0 idle
remains schedulable.
A compile guard requires revalidation if a new SDK watches idle CPU1.

The user supplied their chosen password, now saved to ignored
`.tools/private/admin-password.txt`. The minimum is ten characters to accept
that choice; the KDF work factor is unchanged. Enrollment must read it locally
without echoing or embedding it in a
firmware image. Module/fixture tests use a synthetic known-answer verifier;
product enrollment privately submits the user-selected password over local HTTP.

## Sessions and HTTP

RAM-only sessions: four clients, 30 minute idle and 8 hour absolute expiry,
monotonic millis including wraparound. Session and CSRF values have independent
256-bit randomness. One password job and three admitted attempts per minute
bound work across the device. No permanent lockout. Logout revokes immediately.
Verifier assignment clears all sessions and invalidates unfinished login
results, including a repeat assignment of the same verifier.

| Endpoint | Contract |
| --- | --- |
| POST `/api/auth/login` | JSON with one `password` field; `X-Appliance-Request: 1`; max 512 bytes; returns 202 and a ticket |
| GET `/api/auth/result` | `X-Login-Ticket`; 202 during work, generic 401 on rejection, 200 with Cookie/CSRF on success; 300 s ticket expiry |
| GET `/api/auth/session` | Session Cookie required; returns CSRF for page reload |
| POST `/api/auth/logout` | Session Cookie, `X-CSRF-Token` and matching browser Origin; revokes and expires Cookie |

Cookie: `Path=/; HttpOnly; SameSite=Strict; Max-Age=28800`. v1 trusted-LAN HTTP
or HTTP inside WG does not set Secure; public router forwarding is outside the
transport model. Responses use no-store, nosniff, frame denial and no-referrer.
No permissive CORS. Browser login/mutation Origin must match Host; non-browser
mutations still need session and CSRF. Duplicate/malformed session cookies,
trailing/duplicate JSON, oversized bodies and NUL escapes are rejected. Password
rejections remain generic.

The adapter retains a mutex and worker for the firmware's boot lifetime. Product
handlers must check every protected entry point and use `streamSessionValid`
without refreshing idle expiry inside long streams. New mutations must use POST
and close legacy GET/WebSocket bypasses. The adapter itself does not register
camera, setup, media or OTA handlers.

The offered 80 MHz mode measured 168.09 seconds for real-account login through
WG. A 120-second test-client deadline first failed while restoring the original
240 MHz setting; a bounded 170-second diagnostic retry passed all eight checks,
including actual 80 MHz, authenticated WG/status/JPEG and restoration. This
exceeded the page's prior 150-second wait. The browser wait is now 280 seconds,
and the server ticket lifetime is 300 seconds to leave room for transport.
The 600,000-iteration verifier, session lifetimes and admission limit are unchanged.
Failed and successful diagnostic reports remain separate in ignored
`low-clock-login/`; actual revised-page validation is recorded separately.

## Administrator password changes

The account card has current/new/confirmation masked inputs in Chinese,
English and German. It clears them immediately after submission. No password,
CSRF or result capability is written to browser storage.

| Endpoint | Contract |
| --- | --- |
| GET `/api/account` | Authenticated public policy and pending flag only |
| POST `/api/account/password` | Session, CSRF, matching Origin; exactly three strings `currentPassword`, `newPassword`, `confirmPassword`, flat JSON up to 1536 bytes; policy/matching/different-new checks; 202 with a RAM-only result ticket |
| GET `/api/account/password/result` | Same Origin, `X-Appliance-Request: 1`, `X-Password-Change-Ticket`; 202 pending, single-use 200 success or generic error; no query/body; 600 s capability lifetime |

The existing priority-one password worker first verifies the current password,
then derives a fresh random-salt verifier at 600,000 iterations. Login and
password changes share one bounded expensive-work queue and the three-per-minute
admission limit. Camera/network tasks outrank that work. Network configuration
changes and OTA are excluded while the job is pending; capture remains usable.

The network owner commits only the verified admin payload with an expected
selected revision and no pending network trial. It writes/readbacks the inactive
bank, then changes the selector; it does not reapply Wi-Fi/WG or fabricate network
proof. The originating session and verifier generation are rechecked under the
auth mutex through that short commit. Logout, reset, generation change or expiry
cannot authorize a later commit. Success replaces the RAM verifier and revokes
all sessions and in-flight login generations. Consequently the bounded result
capability, rather than the revoked cookie, reports completion. The capability
has no other account/device authority. Rejected validation/current-password
proof and cancelled work preserve the selected verifier. An interrupted HTTP
result or storage failure needs a subsequent login check to establish the
committed outcome; the UI does not promise an unobserved result.

The real-account rotation gate passes **85 checks**, including originating
logout cancellation, wrong-current proof, two distinct session revocations,
normal-restart persistence, and restoring the chosen password at actual 80 MHz.
The Chrome rotation with concurrent recording takes 94.22 seconds at 240 MHz;
the 80 MHz rotation takes 203.57 seconds. Fifteen status samples take at most
141 ms. The concurrent MJPEG/16 kHz PCM AVI fully decodes with a 4 ms duration
difference. All 78 preexisting media entries and 31 original capture values
remain, with the chosen password and 240 MHz restored and reboot-checked.
Evidence: ignored `account-candidate/report.json`, `password-work.avi` and
`account-candidate-serial.log`. The random temporary recovery password is stored
only in ignored `.tools/private/account-test-password.txt`; it is included in
the private audit and is no longer the selected password. Reports, screenshots,
logs and images contain no credentials/tokens. The final UI-text/image gate passes
**42 further checks**: authenticated OTA, exact assets, nine language/width
layouts, empty-input screenshots, local policy/mismatch rejection, real
wrong-current-password handling and translated live errors. Selected revision
47, all values and camera/SD/WG remain healthy. Evidence: `account-final/report.json`
and its visually inspected screenshots. The core auth/config code is identical
between the two images; only the unconfirmed-result message differs.

## WireGuard import

Bound input to 4096 bytes and one Interface/Peer. Require PrivateKey, Address,
PublicKey, Endpoint and AllowedIPs. Validate optional PSK, DNS, MTU, keepalive
and ListenPort. MTU is 576–1500; the selected backend supports ListenPort 51820
only. DNS currently supports one IPv4 resolver; multiple/IPv6 resolvers fail
explicitly.

Preserve host bits in the IPv4 local address; normalize up to eight IPv4 route
networks. Valid IPv6 addresses/routes are explicitly counted/excluded. Require
at least one IPv4 address/route. Duplicate/unknown fields, multiple peers, hooks,
invalid keys/endpoints and overflow fail atomically: the destination is unchanged.
Support BOM, CRLF, case-insensitive names and comments. Wipe transient config/key
text and never return it in errors. Retain non-WG fields on successful import.

A default route sets `fullTunnelRequested` in the summary but **does not enable
WG-B**. The product manager must implement and verify routing, underlay/local
exceptions and DNS before enabling that mode.

## Hardware evidence

The new root development profile now integrates these checks with the original
camera/recorder and embedded login/dashboard. It passes 67 additional actual
HTTP/camera checks; see [APPLIANCE_HTTP.md](APPLIANCE_HTTP.md). Initial account
enrollment/network trials now also pass the product checks in [SETUP.md](SETUP.md);
no real password was embedded.

- 65 checks: Python PBKDF2 known answer, wrong password/random salt, atomic failed
  enrollment/import, UTF-8/control rejection, CSRF/logout/expiry/capacity,
  millis wrap and rate limiting. No NVS/SD writes.
- 42 real HTTP checks: malformed/cross-origin/unauthenticated requests,
  correct/wrong login, bounded concurrent work, Cookie flags, CSRF, logout,
  existing-session and in-flight-login revocation.
- 33 non-fixture checks: absent active account cannot log in or use protected
  camera/control/status APIs. Nested login JSON is rejected before the parser;
  passwords containing literal brackets or escaped backslashes remain valid JSON.

Login input is preflighted as a flat object before cJSON parsing. Reject
decoded `\u0000` without rejecting the literal six-character `\u0000` text
when its backslash is properly JSON-escaped. Clear root/field string copies
before freeing the parsed request.

- 160 HTTP probes during KDF: maximum 0.138 s; login polling 35.15–35.47 s.
  The isolated utility does not run the camera.
- Actual ignored WG file parsed over USB: one IPv4 route, one excluded IPv6
  route, full tunnel requested but disabled. No NVS/network application.

Ignored evidence: `.tools/hardware/config-auth-test.log`, `config-auth-report.json`,
`auth-http-boot.log`, `auth-http/report.json`, `wg-private-import-report.json`.

```powershell
python scripts/pio.py run -d experiments/hardware -e config-auth -t upload --upload-port COM3
.tools/platformio/penv/Scripts/python.exe -X utf8 scripts/serial_capture.py --port COM3 --seconds 220 --out .tools/hardware/config-auth-test.log --reset --send-when 'CONFIG AUTH TEST READY' --command RUN_CONFIG_AUTH_TEST --stop-when 'CONFIG AUTH TEST COMPLETE'
python scripts/pio.py run -d experiments/hardware -e auth-http -t upload --upload-port COM3
.tools/platformio/penv/Scripts/python.exe -X utf8 experiments/hardware/test_auth_http.py --restore-profile Ouyuan5G
python scripts/pio.py run -e xiao-minimal -t upload --upload-port COM3
```
