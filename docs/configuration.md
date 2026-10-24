# Configuration

Configuration lives in `/config.json` on the LittleFS partition (schema 2).
The captive portal (`Edilkamin_BT_AP`) edits the common fields; the others
can be changed by editing the JSON (upload over the serial console or via the
upcoming web UI) and will get a web form in v1.3.

Files written by v0.x (schema 1) are read and migrated automatically; nothing
has to be re-entered after an upgrade.

## Captive portal fields

The portal appears when no Wi-Fi credentials are stored or the network cannot
be joined (timeout 180 s, then the firmware keeps running and retries the
Wi-Fi in the background).

| Field | Config key | Default |
|-------|------------|---------|
| MQTT server / port / user / password | `mqtt_server`, `mqtt_port`, `mqtt_user`, `mqtt_pass` | – / 1883 / – / – |
| NTP server | `ntp_server` | `pool.ntp.org` |
| Timezone (POSIX) | `tz` | empty → fixed `ntp_offset` (legacy) |
| Stove BLE MAC | `stove_mac` | empty → first stove advertising service `abf0` |
| BLE session length (s) | `ble_session_s` | 30 |
| BLE pause between sessions (s) | `ble_pause_s` | 120 (`0` = stay connected) |

Timezone examples: `CET-1CEST,M3.5.0,M10.5.0/3` (Central Europe),
`GMT0BST,M3.5.0/1,M10.5.0` (UK), `EST5EDT,M3.2.0,M11.1.0` (US East). With a
POSIX TZ string daylight saving time is handled automatically.

## All keys

| Key | Type | Default | Range / notes |
|-----|------|---------|---------------|
| `schema` | int | 2 | written by the firmware |
| `mqtt_server` | string | | ≤ 63 chars |
| `mqtt_port` | int | 1883 | 1–65535 |
| `mqtt_user`, `mqtt_pass` | string | | ≤ 47 / 63 chars |
| `ntp_server` | string | `pool.ntp.org` | |
| `tz` | string | | POSIX TZ |
| `ntp_offset` | int | 7200 | seconds, used only when `tz` is empty |
| `topic_prefix` | string | `edilkamin` | no `/`, `#`, `+` |
| `device_id` | string | `322707E4` | keep it to preserve HA entity ids |
| `stove_mac` | string | | `AA:BB:CC:DD:EE:FF`, empty = auto |
| `ble_enabled` | bool | true | also changed by `bluetooth/set` |
| `ble_session_s` | int | 30 | 10–3600 |
| `ble_pause_s` | int | 120 | 0 or 10–3600 |
| `ble_poll_ms` | int | 2000 | 500–60000, interval between poll reads |
| `ble_response_timeout_ms` | int | 2000 | 500–10000 |
| `ble_reconnect_initial_s` | int | 5 | 1–600, first retry delay |
| `ble_reconnect_max_s` | int | 120 | ≥ initial, backoff cap |
| `ble_scan_timeout_s` | int | 10 | 3–60 |
| `ble_reboot_after_min` | int | 0 | 0 = never; 10–1440 minutes without any successful session before an ESP32 restart (never while BLE is disabled) |
| `data_max_age_s` | int | 360 | ≥ session + pause + 60, ≤ 7200 |
| `command_expiry_s` | int | 120 | 10–3600 |
| `log_level` | int | 3 | 0 OFF, 1 ERROR, 2 WARN, 3 INFO, 4 DEBUG, 5 TRACE |
| `mqtt_log_level` | int | 2 | lines at or below this level go to the `log` topic |
| `telnet_enabled` | bool | false | unauthenticated telnet log viewer on port 23 (LAN only, off by default) |
| `diag_interval_s` | int | 60 | 10–3600 |
| `wifi_reboot_after_min` | int | 15 | 0 = never; minutes without Wi-Fi before a restart |

Values outside their range are clamped at boot and logged
(`config corrected (n): …`).

## Runtime controls (no reboot)

* `log_level/set` – immediate; `DEBUG` and `TRACE` revert after 30 minutes.
* `bluetooth/set` – enable/disable BLE; persisted in `ble_enabled`.
* `diagnostics_reset/set`, `log_dump/set`, `restart/set` – see
  [mqtt-api.md](mqtt-api.md).

## Factory reset

Hold the boot button (GPIO0) while powering the board: `config.json`, the
Wi-Fi credentials and the NVS counters are erased and the portal starts.
