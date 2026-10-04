# Versioned NVS configuration

The `lib/appliance-config` module implements version 1 of a bounded configuration
store using Arduino `Preferences`. It is compiled and hardware-tested on the
pinned Arduino 3.3.12 / IDF 5.5.5 platform. The new camera development profile
uses it through a single owner task for boot, account enrollment and Wi-Fi
settings/trials. Camera-integrated failed Wi-Fi and interrupted-trial reset
checks pass; WG trial/fault-injection gates remain separate.

## Persistence and trial contract

Default namespace: `camcfg`, separate from upstream `ESP-CAM_MJPEG`.
Each envelope contains magic, schema version, length, revision, a fixed payload
and CRC32. Payload fields cover Wi-Fi, IPv4 WG, admin verifier metadata,
device name and storage policy. CRC detects corruption; it is not encryption.

1. `stage` validates bounded fields and writes/readbacks `pending`. A persistent
   revision high-water mark prevents reused confirmation tokens after reboot.
2. `beginTrial` commits the attempted revision before the caller changes Wi-Fi
   or routes. The caller must keep a local recovery path available.
3. `confirmTrial` requires the same live revision, local-access evidence,
   Wi-Fi connectivity when configured, a valid peer session when WG is enabled,
   and route-validation evidence for a requested full tunnel.
4. Promotion writes/readbacks the inactive `active0`/`active1` bank, then commits
   `selected` last. Cleanup of pending metadata happens after that commit.
5. A boot following an interrupted trial selects the old active configuration
   and reports `pending_trial_interrupted`. It does not retry or accept that
   candidate automatically. First boot without active configuration requires setup.

Only one task may own a store instance; the product manager must serialize
updates. `ConnectivityProof` is supplied by that manager, not by an HTTP client.
The store does not itself measure network connectivity. Runtime failures must
restore the old network configuration and discard or retain the failed pending
revision for diagnosis through the manager.

Administrator password rotation uses `updateAdmin`, a verifier-only compare-and-
swap against the selected revision when no pending/live trial exists. It retains
every other selected field, reserves a monotonic revision, writes/readbacks the
inactive bank and selects it last. Only the network owner calls this method,
after asynchronous current-password proof and new-verifier generation. This
operation neither applies network changes nor invents connectivity evidence.

If the selected bank fails CRC/schema validation, a previous valid bank can be
recovered. Unknown schema versions are preserved and write-protected for
recovery; no automatic namespace erasure is used. A future schema change must
add an explicit decoder/migration before increasing `CONFIG_VERSION`.

## Secrets and migration

`MaskedStatus` is an explicit public allowlist containing presence flags and
nonsecret device/SSID fields. It contains no Wi-Fi password, WG key, admin salt
or password verifier. Internal `Config` access is for network/auth consumers;
it must never be returned wholesale from status/export endpoints. New payloads
normalize string tails and omit inactive admin-verifier material.

Wi-Fi migration reads bounded `ST_SSID`/`ST_Pass` from the legacy namespace into
**pending**, requiring normal validation before activation. Legacy keys are
preserved. The old plaintext admin password is not copied; new administrator
enrollment is required. Ordinary NVS is not hardware-secure secret storage.
`AdminAuth` now implements salted password enrollment/verification and volatile
sessions using these fields; `HttpAuth` adds a tested asynchronous HTTP adapter.
The store itself never hashes passwords or admits HTTP clients. Product
enrollment and password changes use the single network owner. See
[AUTHENTICATION.md](AUTHENTICATION.md).

## Real hardware evidence and limits

The isolated `config-nvs` utility uses synthetic credentials and only owned
`cfgtest`, `cfgmigtest`, `cfglegacytest`, `cfgtestctl` namespaces. It checks board
identity, requires a USB start command and cleans up only its marked fixtures.
Upstream/product namespaces and SD data remain untouched.

The suite passed five actual software reboots and persisted checkpoints for:

- initial promotion, active persistence and a second bank;
- interrupted trial rollback and prevention of automatic retry/late confirmation;
- stale revision and missing Wi-Fi/WG/full-tunnel proof rejection;
- a fault injected after bank write but before selector commit;
- CRC corruption and recovery of the previous bank;
- legacy import, preservation of legacy values and new admin enrollment;
- future-schema rejection with byte-for-byte preservation;
- malformed strings/keys/IPv6 rejection and public secret-presence flags.

The first trial reproduced stack overflow in the test fixture's large temporary
buffers. The fixture now uses static scratch space; normal records are read
directly into the destination and unknown-length envelopes use bounded heap
storage. The corrected suite passed without assertion/panic. Evidence remains
in ignored `.tools/hardware/config-nvs-test.log` and `config-nvs-report.json`.

Connectivity proofs in the 78-check suite are simulated. The subsequent
[network manager gate](NETWORK_TRIAL.md) passes 37 real Wi-Fi/LAN/AP checks,
including failed-network rollback and reset during an unconfirmed trial, using
only owned test namespaces. Root camera/setup integration and physical power
removal during flash writes remain acceptance tests. NVS exhaustion/write failures preserve the selected bank
by construction but have not yet been fault-injected on hardware.

```powershell
python scripts/pio.py run -d experiments/hardware -e config-nvs -t upload --upload-port COM3
.tools/platformio/penv/Scripts/python.exe -X utf8 scripts/serial_capture.py --port COM3 --seconds 100 --out .tools/hardware/config-nvs-test.log --reset --send-when 'CONFIG NVS TEST READY' --command RUN_CONFIG_NVS_TEST --stop-when 'CONFIG NVS TEST COMPLETE'
python scripts/pio.py run -e xiao-minimal -t upload --upload-port COM3
```
