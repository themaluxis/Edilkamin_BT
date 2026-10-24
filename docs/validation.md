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

## Web administration (v1.3)

| # | Scenario | Expected |
|---|----------|----------|
| W1 | Fresh install, no password in the portal, open `http://<ip>/` | setup form; after setting a password the status page loads; `config.json` contains `admin_hash` (32 hex), never the password |
| W2 | Wrong password 5 times | `429` for 30 s, `WEB: auth failed` lines in the log, `web_auth_failures` counter increments |
| W3 | Change `log_level` to DEBUG from the page | applies immediately, `log_level/state` publishes `DEBUG`, reverts after 30 min |
| W4 | Change the MQTT broker | answer flags *Restart required*, banner on Status; after restart the new broker is used |
| W5 | Export, factory reset, re-provision, import | every non-secret key restored; MQTT password must be re-entered |
| W6 | Wi-Fi change to a wrong password | module restarts, `Edilkamin_BT_AP` visible for 3 min, correct credentials from the portal recover it |
| W7 | Switch *Web administration* off in Home Assistant | port 80 closed within a second; switch on restores it without reboot |
| W8 | 48 h with the status tab open in a browser (5 s refresh) | `min_free_heap` stays stable (no leak), BLE sessions unaffected |
| W9 | Request while a BLE connection is in progress | answer delayed by at most the connection timeout, no reset, no stove command lost |

## OTA (v1.4)

| # | Scenario | Expected |
|---|----------|----------|
| O1 | USB flash of v1.4 over a v1.3 module without erasing | Wi-Fi rejoins by itself, page asks for a password, import restores settings, *OTA capable: yes* |
| O2 | Upload the same `firmware.bin` from the page | checks pass, version shown, reboot into the other slot, `ota_slot` toggles, `ota_pending` true then false once MQTT connects, diagnostics refreshed |
| O3 | Upload a LittleFS image, a random file, an ESP32-S3 image, a truncated firmware | each refused with the documented reason; running firmware untouched; no reset |
| O4 | Upload a build with a deliberate crash in `setup()` | bootloader rolls back at the first reset; previous version answers again; other slot state `aborted` |
| O5 | Upload a build whose MQTT is broken on purpose, do nothing | still running after 10 min and validated (no silent rollback without a reset); manual rollback works |
| O6 | Power cut during the upload | old firmware boots; next upload succeeds |
| O7 | Upload while a BLE session is active | transfer completes; session resumes or restarts after; no command lost or duplicated |
| O8 | WebFlash the v1.4 manifest on a module currently booting `app1` | boots `app0`, configuration kept |

## Home Assistant identity (v1.5)

| # | Scenario | Expected |
|---|----------|----------|
| H1 | Upgrade a v1.4 module | same entity ids as before, new *Power* select and *Republish discovery* button, the v0.x *AirKare* switch disappears, device page shows *Visit* (configuration_url) and the two MAC connections |
| H2 | Fresh module (erase flash) | device id = last 3 bytes of the MAC, topics under `edilkamin/<id>/`, single device in HA |
| H3 | Change `device_id` on the page, restart | old entities and retained states gone within seconds of the MQTT connection, new device complete; `ha_published_id` updated |
| H4 | Turn *Relax switch* off | `switch.*relax*` removed from HA immediately; `relax/set ON` answers `REJECTED relax not supported by profile`; turning it on brings the entity back |
| H5 | Select P3 in *Power* | `power/set P3` → `VERIFIED`, `power/state P3`, climate preset shows `Man. P3` |
| H6 | Delete the device in HA, press *Republish discovery* | device recreated with every entity and current states |
| H7 | Change hostname to `stove-salon` | after restart `http://stove-salon.local/` answers, status page shows the name |

## External thermostat (v2.0)

| # | Scenario | Expected |
|---|----------|----------|
| T1 | Enable the thermostat, no measurement published yet | status `waiting_sensor`, no command sent, climate setpoint unchanged |
| T2 | Publish 18.0 with target 20.0, stove probe 21.0 | one `SET_TARGET_TEMP` to 23.0 (`VERIFIED`), status `heat`, stove starts modulating up on its own |
| T3 | Publish 20.7 | after the minimum interval one write to probe − 1.0, status `idle`; no `off` command ever appears in `command/result` |
| T4 | Stop publishing for 11 min | status `sensor_lost`, exactly one write restoring the setpoint noted at takeover, then silence |
| T5 | Reboot the ESP32 with the thermostat enabled | nothing written until a fresh measurement arrives |
| T6 | Change the climate setpoint from the card while enabled | `REJECTED external thermostat active`, card snaps back |
| T7 | Disable the thermostat | previous setpoint restored once |

## Experimental registers and register tool (v2.0)

| # | Scenario | Expected |
|---|----------|----------|
| R1 | `register_read/set 0x02E4` | `register/result` within the session with the same word as `status_raw/state` |
| R2 | `register_read/set 0x02F4,2` | two values, second byte matches the pellet state |
| R3 | `register_read/set 0x02E6,4` | rejected (`count must be 1..3`), nothing sent |
| R4 | Enable experimental registers | poll cycle grows by 3 (4 with 2+ fans), raw sensors appear in HA, `requests_ok` keeps increasing, no timeouts attributable to the new registers |
| R5 | Trigger an alarm (e.g. open the door if the model allows) | `status_raw` changes even if the label stays, *Alarm code (exp.)* changes; note the values |
