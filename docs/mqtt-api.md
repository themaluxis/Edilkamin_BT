# MQTT API

All topics live under `<topic_prefix>/<device_id>/` (default
`edilkamin/322707E4/`). Topic names and Home Assistant `unique_id`s of the
v0.x firmware are unchanged, so upgraded devices keep their entity ids.

## Availability model (three separate facts)

| Topic | Payload | Retained | Meaning |
|-------|---------|----------|---------|
| `availability/state` | `ONLINE` / `OFFLINE` | yes (MQTT Last Will) | the ESP32 is connected to the broker |
| `ble/connected` | `ON` / `OFF` | yes | a GATT link with the stove exists right now |
| `data_valid/state` | `ONLINE` / `OFFLINE` | yes | a valid stove answer was received within `data_max_age_s` |

Stove entities (climate, switches, stove sensors) are *available* only when
both `availability/state` and `data_valid/state` are `ONLINE`
(`availability_mode: all`). Diagnostic entities only need the ESP32 to be
online, so they stay visible while the stove is unreachable.

## State topics (retained, published on change only)

| Topic | Payload |
|-------|---------|
| `temperature/state` | room temperature, e.g. `20.4` |
| `target_temperature/state` | setpoint, e.g. `19.5` |
| `thermocouple_temperature/state` | exhaust temperature in whole °C |
| `status/state` | `Off`, `Cleaning`, `Loading Pellets`, `Ignition - charge suspend`, `Check exhaust gas temperature`, `Threshold temperature check`, `Warmup`, `Ok`, `Shutdown`, `Extinction` |
| `hvac_mode/state` | `heat` / `off` (derived from the status, or from the on/off echo) |
| `hvac_action/state` | `off`, `preheating`, `heating`, `idle` |
| `fan_mode/state` | `Auto`, `20%`, `40%`, `60%`, `80%`, `100%` |
| `preset_mode/state` | `Auto`, `Man. P1` … `Man. P5` |
| `automatic_mode/state`, `relax/state`, `standby/state`, `chrono_mode/state` | `ON` / `OFF` |
| `pellet_level/state` | `Ok` / `Empty` |
| `bluetooth/state` | `ON` / `OFF` – BLE *enabled* setting (persisted) |
| `web_admin/state` | `ON` / `OFF` – local web administration enabled (persisted) |
| `ble/state` | `DISABLED`, `IDLE`, `SCANNING`, `CONNECTING`, `CONNECTED`, `COMMUNICATING`, `ERROR` |
| `last_update/state` | ISO 8601 UTC timestamp of the last valid stove answer |
| `log_level/state` | `OFF`, `ERROR`, `WARN`, `INFO`, `DEBUG`, `TRACE` |
| `diagnostics/state` | JSON document, see below |

Unknown or implausible readings are not published; the previous value stays
and, once `data_max_age_s` elapses without any valid answer, the stove
entities become unavailable instead of showing stale data as current.

## Command topics (`<entity>/set`)

Commands are **never retained**. On every connection the firmware deletes
retained messages on all command topics *before* subscribing, so a stale
retained `heat` can never ignite the stove after a reboot.

| Topic | Accepted payloads | Notes |
|-------|-------------------|-------|
| `hvac_mode/set` | `heat`, `off` | ignition / shutdown |
| `target_temperature/set` | number `15` … `30`, e.g. `17`, `17.0`, `17.5` | strict numeric parsing, rounded to 0.1 °C |
| `fan_mode/set` | `Auto`, `20%` … `100%` | refused until the power level is known |
| `preset_mode/set` | `Auto`, `Man. P1` … `Man. P5` | refused until the fan level is known |
| `automatic_mode/set`, `relax/set`, `standby/set`, `chrono_mode/set` | `ON`, `OFF` | |
| `bluetooth/set` | `ON`, `OFF` | enables / disables BLE; persisted |
| `web_admin/set` | `ON`, `OFF` | starts / stops the local web page; persisted in `web_enabled` |
| `log_level/set` | `OFF` … `TRACE` | `DEBUG`/`TRACE` revert to the previous level after 30 min |
| `log_dump/set` | `DUMP` | replays the in-memory log buffer on `log` |
| `diagnostics_reset/set` | `RESET` | resets counters and last error |
| `restart/set` | `RESTART` | restarts the ESP32 (the stove is not touched) |

Anything else is rejected with a reason (see below) and counted in
`commands_rejected`.

## Command results

Every user command publishes its lifecycle on `command/result` (not
retained):

```json
{"id":12,"type":"SET_TARGET_TEMP","entity":"target_temperature","payload":"19.5",
 "state":"VERIFIED","reason":"read back 0x00C3, expected 0x00C3"}
```

`state` is one of `QUEUED`, `SENT`, `ACKNOWLEDGED`, `VERIFIED`, `REJECTED`,
`TIMEOUT`, `FAILED`. A command always ends in `VERIFIED`, `REJECTED`,
`TIMEOUT` or `FAILED`; nothing disappears silently. Failures also update
`last_error` in the diagnostics document.

## Diagnostics document

`diagnostics/state` (retained, every `diag_interval_s` and on events):

| Key | Description |
|-----|-------------|
| `fw` | firmware version (`git describe`) |
| `ble_state`, `ble_connected`, `ble_enabled`, `stove_mac` | BLE state machine, link, setting, address in use |
| `ble_connect_attempts`, `ble_sessions_ok`, `ble_disconnects`, `ble_scan_not_found` | BLE counters |
| `requests_ok`, `requests_failed`, `frames_invalid` | request / frame counters (CRC, padding, key errors) |
| `commands_rejected`, `commands_failed` | user command counters |
| `queue_depth`, `queue_capacity`, `queue_overflows` | command queue |
| `mqtt_reconnects`, `wifi_reconnects`, `rssi`, `ip` | network |
| `uptime_s`, `free_heap`, `min_free_heap` | ESP32 health |
| `boot_count`, `reset_reason`, `last_restart_cause` | reboot tracking (`last_restart_cause` is set by the firmware before an intentional restart: `wifi watchdog`, `ble watchdog`, `mqtt restart command`, `web restart`, `wifi credentials changed`) |
| `last_error`, `last_error_at` | last error message and time |
| `last_valid_at`, `data_valid`, `ntp_synced`, `log_level` | data freshness / time / logging |

## Logs over MQTT

`log` (not retained) receives log lines at or below `mqtt_log_level`
(default `WARN`) and the full buffer when `log_dump/set` is pressed. Lines
look like `12:34:56 [   123.456] W BLE: error: stove not found (retry in 5000 ms)`.
Secrets are never logged.

## Home Assistant entities

| Entity | Component | unique_id suffix | Category |
|--------|-----------|------------------|----------|
| Edilkamin (climate) | climate | `climate` | – |
| Automatic Mode, Relax, Chrono, Standby | switch | `automatic_mode`, `relax`, `chrono_mode`, `standby` | – |
| Status, Exhaust Temperature, Pellet Level | sensor | `status`, `thermocouple_temperature`, `pellet_level` | – |
| Room Temperature | sensor | `room_temperature` | – |
| Bluetooth connect | switch | `bluetooth` | config |
| Web administration | switch | `web_admin` | config |
| Log level | select | `log_level` | config |
| Restart ESP32 | button | `restart` | config |
| Stove connected, Stove data fresh | binary_sensor | `ble_connected`, `data_valid` | diagnostic |
| Bluetooth state, Last stove response, Wi-Fi signal, Last error, Firmware, Uptime, Free memory, Boot count, Reset reason, Command queue, BLE connect attempts, BLE disconnects, Requests OK, Requests failed | sensor | `ble_state`, `last_update`, `wifi_rssi`, `last_error`, `firmware`, `uptime`, `free_heap`, `boot_count`, `reset_reason`, `queue_depth`, `ble_connect_attempts`, `ble_disconnects`, `requests_ok`, `requests_failed` | diagnostic |
| Dump logs to MQTT, Reset counters | button | `log_dump`, `diagnostics_reset` | diagnostic |

`unique_id` = `<device_id>_edilkamin_<suffix>`. Discovery documents are
published on every MQTT connection and whenever Home Assistant announces
itself on `homeassistant/status`.
