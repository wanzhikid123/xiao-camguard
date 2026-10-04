# Network configuration trial manager

Updated 2026-10-03. The root appliance now uses this module through an owning
`NetworkService` task and authenticated command mailbox. Product enrollment,
failed-Wi-Fi rollback while recording and interrupted-trial reboot pass.
See [SETUP.md](SETUP.md). The WireGuard driver remains a separate integration.

`NetworkManager` owns a `ConfigStore` and an injected `NetworkDriver`. One task
must own all calls; HTTP adapters queue commands to that owner. Driver methods
initiate work without waiting for connection. The caller advances the state
machine with monotonic `millis()` timestamps. Public snapshots contain only
masked configuration, state/error names and connectivity flags.

- Empty configuration starts setup AP.
- Stored active configuration connects without changing its selected bank.
- Wi-Fi loss starts a fresh connection window, retries every 15 seconds and
  enters rescue AP after 60 seconds without a connection.
- Submitting a candidate validates before writes, then stages it and commits
  its tried marker before changing Wi-Fi or routes. Overlapping trials fail.
- A connection must remain ready for three seconds. Promotion additionally
  requires a real authorized request reaching the candidate LAN address.
  Host/header assertions and AP reachability are not proof of that LAN path.
- Wi-Fi failure after 60 seconds or missing confirmation after 90 seconds
  restores the previous active configuration. Both `tick()` and confirmation
  enforce the expiry deadline. First-run failure returns to setup AP.
- A reboot discards unconfirmed pending data and reconnects the selected
  active configuration; it never retries a marked candidate automatically.
- Rescue AP ends after normal active connectivity recovers. First setup and
  ongoing trials retain AP. The optional inactivity timeout is not implemented.
- Unknown schema/storage failures preserve existing data and expose recovery;
  they do not erase or silently promote configuration.

The current `WifiDriver` consumes the original 33-byte SSID/65-byte password
buffers directly, disables SDK persistence/automatic reconnect, and does not
contain a competing gateway-ping owner. It explicitly refuses WireGuard or
full-tunnel trials. It uses the pinned SDK's actual AP end operation:
`softAPdisconnect(false)` only clears the SSID; `true` ends AP mode. None of
these settings modifies watchdog policy.

## Hardware gate

The explicit `network-trial` experiment checks the known board and owns only
`nettrial` and `nettrialctl`. It refuses unmarked existing namespace contents.
Working Wi-Fi credentials and a random temporary HTTP test token arrive over
USB; neither is embedded in source/firmware or saved in the report. The token
is temporarily persisted in the owned fixture namespace solely to cross its
deliberate reset; cleanup removes both marked namespaces. Production `camcfg`,
upstream configuration and SD are untouched.

The HTTP proof adapter compares the socket's actual local IP with the current
station IP. Tests use the recovery AP to verify refusal, restore the computer's
original network, then confirm over the real LAN. A second connection attempt
uses a nonexistent SSID; subsequent tests reset during a marked trial and
disconnect Wi-Fi deliberately. All 37 checks pass: 30 board checks and 7 real
HTTP/host checks. AP confirmation is rejected, LAN confirmation promotes the
candidate, failed Wi-Fi restores the selected credentials, a real software
reset discards the interrupted candidate, and LAN HTTP works after reconnect.
Both marked namespaces are removed, the PC profile is restored and its helper
profile is deleted. Evidence: ignored `.tools/hardware/network-trial-report.json`.
The fixture firmware is 914,160 bytes, static RAM 53,980 bytes; its archived
binary and ELF are also under `.tools/hardware`.

The first fixture boot exposed an SDK assertion when `WebServer::begin()` ran
before network initialization. A decoded backtrace identified `lwip_socket`
through `NetworkServer::begin`, called by the fixture's setup. The fixture now
starts HTTP only after the manager starts its network. This was before the
private input handshake or test configuration writes, and does not establish a
product boot failure. Initial boot/report evidence is kept privately.

```powershell
python scripts/pio.py run -d experiments/hardware -e network-trial -t upload --upload-port COM3
.tools/platformio/penv/Scripts/python.exe -X utf8 experiments/hardware/test_network_trial.py --port COM3 --ssid Ouyuan --restore-profile Ouyuan5G
python scripts/pio.py run -e xiao-minimal -t upload --upload-port COM3
```

Camera/recording coexistence, first enrollment and product Wi-Fi rescue now pass
in the separate product run. Actual WG trial rollback and full-tunnel proof
remain required; the Wi-Fi experiment alone cannot complete those gates.
