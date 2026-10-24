# Validation on a real stove

Unit tests cover the pure logic (`pio test -e native`). The scenarios below
come from the specification (§9.2) and must be run on hardware before a
release. Record the outcome, firmware version and stove model in the PR.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| 1 | Normal cycle | power up, wait 10 min | `ble/state` cycles IDLE/SCANNING/CONNECTING/CONNECTED/COMMUNICATING/IDLE; no repeated identical publishes on the broker (`mosquitto_sub -v`) |
| 2 | Temperature from HA | set 19 °C | `command/result`: QUEUED, SENT, ACKNOWLEDGED, VERIFIED; `target_temperature/state` = 19.0 within ~10 s |
| 3 | Invalid command | publish `abc` on `target_temperature/set`; publish `31` | REJECTED with reason; nothing sent to the stove (no `SENT`) |
| 4 | Change from The Mind remote | change the setpoint on the remote | new value in HA at the next session, no command emitted by the ESP32 |
| 5 | Stove out of range | power the stove off (or shield it) | `ERROR` with `stove not found`, backoff 5 → 120 s visible in logs; after `data_max_age_s` the climate is unavailable; diagnostics keep updating; **no ESP32 reboot** |
| 6 | Stove back | power it on | reconnects at the next retry; `data_valid` back ONLINE |
| 7 | Wi-Fi loss | unplug the router for 5 min | reconnects without reboot; counters `wifi_reconnects` increase; config intact |
| 8 | Broker restart | restart mosquitto | reconnect with backoff; discovery + all states republished; HA entities consistent |
| 9 | HA restart | restart Home Assistant | on `homeassistant/status online` everything is republished |
| 10 | Bluetooth disabled | `bluetooth/set OFF` for 30 min | `DISABLED`, no reconnection attempts, no watchdog restart, commands REJECTED `bluetooth disabled`; app can connect to the stove |
| 11 | Queue saturation | publish 20 commands in a burst while the stove is off | superseded ones REJECTED, later ones FAILED `command queue full` or expired after `command_expiry_s`; `queue_overflows` counts |
| 12 | ESP32 reboot | press *Restart ESP32* with a retained `heat` left on `hvac_mode/set` by hand | the retained message is cleared; the stove is **not** ignited; `last_restart_cause` = `mqtt restart command` |
| 13 | Remote diagnostics | set log level TRACE from HA, dump logs | raw frames visible on `log`; level reverts after 30 min |
| 14 | 48 h soak | leave running 48 h | `boot_count` unchanged, `frames_invalid` ≈ 0, `min_free_heap` stable, no unexplained ERROR streaks |
| 15 | Room temperature > 25.5 °C | heat the remote's sensor | correct reading (not wrapped), see troubleshooting |

Measure and note: time from command to VERIFIED while connected / while idle,
scan-to-COMMUNICATING time, RSSI at the stove location.

## BLE connection regression after v1.1

Use the same ESP32, stove, location and saved configuration that worked
before the upgrade. Upload without erasing flash or resetting pairing.
Record whether the earlier firmware still connects if a baseline binary
is available; keep that binary's partition layout in mind when switching.

The corrected manager waits for the scan-complete event before connecting,
allows the original 30-second connection timeout, and starts the session's
no-data deadline after GATT setup. Verify that it reaches `GATT ready` and
then `COMMUNICATING`, and that retry delays start after a failed connection
returns. If it still fails, record the decimal/hex code from Home Assistant's
*Last error*, the stove RSSI from `connecting to`, and the USB `NimBLEClient`
lines. A successful build is not a hardware pass for this scenario.
