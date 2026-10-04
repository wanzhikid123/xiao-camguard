# First setup and Wi-Fi repair

Updated 2026-10-03. Implemented in the non-fixture `xiao-appliance` environment;
product account/Wi-Fi hardware gates pass. WireGuard, media and OTA remain
separate integrations. Never use the synthetic auth fixture as a product build.

The embedded Chinese page opens first setup when no selected admin account
exists. The setup AP is `XIAO-Camera-084C`, at `http://192.168.4.1`. Enter the
2.4 GHz SSID, its password and the chosen admin password. Passwords are not
trimmed by the device; the admin minimum is ten Unicode characters and the
maximum is 128 UTF-8 bytes. PBKDF2-HMAC-SHA256 remains at 600,000 iterations.

The owner task performs the expensive password work on CPU1 at priority one,
then validates/stages the complete configuration. The tried marker is committed
before changing Wi-Fi. Candidate connection has a 60-second timeout and a
90-second total confirmation deadline. The AP remains available during the
trial. Credentials and the salted verifier are private NVS payloads; ordinary
NVS does not encrypt Wi-Fi/WireGuard credentials.

When Wi-Fi connects and remains ready for three seconds, the page displays its
LAN address. Reconnect the computer to that Wi-Fi and open the confirmation
link. A random 256-bit setup capability is handed off in the URL fragment, never
sent in the HTTP URI. The page removes the fragment from its current history
entry immediately, keeps the capability only in memory, and sends it in a
header. It expires after three minutes. Do not share the confirmation link.

The confirmation handler uses the socket's actual local destination, including
the SDK's IPv4-mapped dual-stack representation. Merely claiming a LAN address
in a Host header, or accessing the recovery AP, cannot confirm the candidate.
The owner rechecks Wi-Fi, address, stable readiness, revision and deadline before
promoting the bank selector. Only then does the auth service load the verifier.
The selected account cannot be overwritten through the initial setup endpoint.

All camera/status/settings controls remain locked until a normal login succeeds.
The initial endpoints require the random nonce/ticket, request marker and same
origin, with no CORS. Bodies are bounded flat JSON: no nested data, duplicate or
unknown fields, embedded NUL, truncated strings or secrets in status responses.
HTTP defers interface mutations for 750 ms so its accepted response can leave.

Wi-Fi repair is available after login. It preserves the active admin/WG/app
payload, stages only supplied Wi-Fi fields, and uses the same LAN confirmation
contract. The new IP needs a fresh browser login because cookies are scoped to
the host. Failure or reboot during a trial keeps the selected configuration;
the unconfirmed candidate is discarded at boot. Wi-Fi-only promotion does not
invalidate existing sessions. The AP closes when the selected Wi-Fi recovers.
The optional ten-minute inactive AP expiry and BOOT-button recovery are not
implemented.

Reproducible test:

```powershell
python scripts/pio.py run -e xiao-appliance -t upload --upload-port COM3
.tools/platformio/penv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_setup.py --ssid Ouyuan --restore-profile Ouyuan5G
```

The harness reads ignored `.tools/private/admin-password.txt` and
`wifi-password.txt` locally. It does create the chosen product account via HTTP;
it never embeds passwords into firmware, exports the account, or prints/saves
cookies/tickets. A failed-Wi-Fi trial runs during recording. A second candidate
is interrupted by a normal authenticated restart. It restores the PC's original
profile and removes only its temporary helper profile. Use `--resume-lan` to
continue validation of an already-enrolled device without resetting its account.

Evidence is kept under ignored `.tools/hardware/appliance-setup-report.json`
and `appliance-setup-boot.log`. The initial pre-write probe caught missing setup
access because the adapter treated the dual-stack HTTP socket as IPv4-only;
the adapter now accepts mapped IPv4 while rejecting native IPv6 as proof.

The initial enrollment run passed 37 assertions: private nonce and bounded input,
AP confirmation refusal, real LAN proof, account activation, real-password login
and a normal recovery restart. Its later AP visit timed out on the PC, before
the rollback gate finished. The host now retries only AP connection/GETs, never
mutations. Resume also finalizes recordings left by an interrupted host run and
waits for the owner's real three-second stable condition rather than asserting
ready at the first connected packet. Those earlier reports remain preserved.

The final resumed run passes all 32 checks: real login, camera/SD/audio readiness,
recording throughout the 60-second bad-Wi-Fi trial, AP authentication, selected
revision unchanged, recovery AP closure, recording finalization, another trial
interrupted by authenticated restart, old session revocation, persisted password
login and no pending-candidate retry. It ends logged out on confirmed Wi-Fi,
revision 1, LAN `192.168.1.181`. Password logins take 43.430 and 42.317 seconds;
maximum whole-harness HTTP elapsed time is 3.653 seconds, including transitions.
The PC profile and temporary helper cleanup pass. Final evidence:
`appliance-setup-resume-report.json`; initial account evidence:
`appliance-setup-enrolled-first-report.json`. This is Wi-Fi/account acceptance,
not integrated WireGuard, physical power-loss or long-term leak acceptance.

The verified firmware is 1,746,384 bytes (52.25% of each OTA slot), static RAM
105,388 bytes. Source/binary/log audits exclude all five supplied local private
values. The actual room JPEGs and recordings stay under ignored storage.
