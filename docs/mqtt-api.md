# MQTT API

All topics live under `<topic_prefix>/<device_id>/`. Topic names and Home
Assistant `unique_id`s of the v0.x firmware are unchanged, so upgraded devices
keep their entity ids.

## Device identity

| Situation | `device_id` |
|-----------|-------------|
| Upgraded from v0.x–v1.4 (a `config.json` exists) | kept as stored, historically `322707E4` |
| Fresh module (no configuration file, or after a factory reset) | last three bytes of the ESP32 MAC, e.g. `A1B2C3`, so several modules never collide |

`device_id` and `topic_prefix` can be changed from the web page (restart
required). On the next MQTT connection the firmware deletes every discovery
document and retained state topic of the previous identity, then publishes
the new ones, so Home Assistant shows one device and no orphans. The last
published identity is remembered in `ha_published_prefix` / `ha_published_id`.
Entity history in Home Assistant is tied to the `unique_id`, so changing the
id starts new entities; keep the historical id if you want continuity.

The device document carries `identifiers: [device_id]`, `connections`
(ESP32 MAC and, once known, the stove's Bluetooth address),
`configuration_url` (the web page), `sw_version` and `hw_version`.

Obsolete documents are cleaned automatically: the v0.x `airkare` switch is
removed at every start (unless a profile enables AirKare), and switches
disabled with `feat_relax` / `feat_standby` / `feat_chrono` are removed
when the setting changes.

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
| `power/state` | `P1` … `P5` (power level alone, for the *Power* select) |
| `status_raw/state` | raw main status word, e.g. `0x0202` (unknown combinations stay visible) |
| `ext_thermostat/state`, `ext_setpoint/state`, `ext_temperature/state`, `ext_status/state` | external thermostat, see [external-thermostat.md](external-thermostat.md) |
| `alarm_raw/state`, `pellet_remaining_raw/state`, `controller_version_raw/state`, `fan2_raw/state`, `fan3_raw/state` | experimental raw registers, only with `exp_registers`, see [the-mind-registers.md](the-mind-registers.md) |
| `register/result` | not retained; answer of the register read tool |
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
| `power/set` | `P1` … `P5` (or `1` … `5`) | same register as the fan; refused until the fan level is known |
| `automatic_mode/set`, `relax/set`, `standby/set`, `chrono_mode/set` | `ON`, `OFF` | |
| `bluetooth/set` | `ON`, `OFF` | enables / disables BLE; persisted |
| `web_admin/set` | `ON`, `OFF` | starts / stops the local web page; persisted in `web_enabled` |
| `log_level/set` | `OFF` … `TRACE` | `DEBUG`/`TRACE` revert to the previous level after 30 min |
| `log_dump/set` | `DUMP` | replays the in-memory log buffer on `log` |
| `diagnostics_reset/set` | `RESET` | resets counters and last error |
| `restart/set` | `RESTART` | restarts the ESP32 (the stove is not touched) |
| `discovery_republish/set` | `PUBLISH` | re-sends every discovery document and state (after deleting the device in Home Assistant, for instance) |
| `ext_thermostat/set` | `ON`, `OFF` | external thermostat (mode A); persisted |
| `ext_setpoint/set` | number in the profile range | room target for the external thermostat; persisted |
| `ext_temperature/set` | number −30 … 60 | external measurement; must be refreshed within `ext_sensor_max_age_s` |
| `register_read/set` | `0x02E6` or `0x02E6,2` | diagnostic read of 1–3 registers (function 3 only), one every 2 s |

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
| `ota_slot`, `ota_pending` | running application slot (`app0`/`app1`) and whether the image still awaits validation after an update |

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
| Power | select | `power` | – |
| External thermostat | switch | `ext_thermostat` | – |
| External setpoint, External temperature | number | `ext_setpoint`, `ext_temperature` | – |
| External thermostat status, Status (raw) | sensor | `ext_status`, `status_raw` | diagnostic |
| Alarm code, Pellets remaining, Controller version, Fan 2, Fan 3 (all *exp.*) | sensor | `alarm_raw`, `pellet_remaining_raw`, `controller_version_raw`, `fan2_raw`, `fan3_raw` | diagnostic, only with `exp_registers` |
| Bluetooth connect | switch | `bluetooth` | config |
| Web administration | switch | `web_admin` | config |
| Log level | select | `log_level` | config |
| Restart ESP32, Republish discovery | button | `restart`, `discovery_republish` | config |
| Stove connected, Stove data fresh | binary_sensor | `ble_connected`, `data_valid` | diagnostic |
| Bluetooth state, Last stove response, Wi-Fi signal, Last error, Firmware, Uptime, Free memory, Boot count, Reset reason, Command queue, BLE connect attempts, BLE disconnects, Requests OK, Requests failed | sensor | `ble_state`, `last_update`, `wifi_rssi`, `last_error`, `firmware`, `uptime`, `free_heap`, `boot_count`, `reset_reason`, `queue_depth`, `ble_connect_attempts`, `ble_disconnects`, `requests_ok`, `requests_failed` | diagnostic |
| Dump logs to MQTT, Reset counters | button | `log_dump`, `diagnostics_reset` | diagnostic |

`unique_id` = `<device_id>_edilkamin_<suffix>`. Discovery documents are
published on every MQTT connection, whenever Home Assistant announces itself
on `homeassistant/status`, and on *Republish discovery*. Relax, Standby and
Chrono are only published when the matching `feat_*` setting is on; their
documents are deleted otherwise.
