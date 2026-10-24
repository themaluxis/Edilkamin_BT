# Configuration

Configuration lives in `/config.json` on the LittleFS partition (schema 4).
The captive portal (`Edilkamin_BT_AP`) edits the common fields; **every** key
can be edited from the local web page (`http://edilkaminble.local/`, see
[web-ui.md](web-ui.md)), which also exports and imports the file.

Files written by v0.x (schema 1), v1.1 (schema 2) and v1.3 (schema 3) are
read and migrated automatically; nothing has to be re-entered after an
upgrade.

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
| Web admin password | `admin_hash` (stored hashed) | empty → web administration locked until set |

Password fields are never pre-filled with the stored value; leaving them
empty keeps the current password.

Timezone examples: `CET-1CEST,M3.5.0,M10.5.0/3` (Central Europe),
`GMT0BST,M3.5.0/1,M10.5.0` (UK), `EST5EDT,M3.2.0,M11.1.0` (US East). With a
POSIX TZ string daylight saving time is handled automatically.

## All keys

| Key | Type | Default | Range / notes |
|-----|------|---------|---------------|
| `schema` | int | 4 | written by the firmware |
| `mqtt_server` | string | | ≤ 63 chars |
| `mqtt_port` | int | 1883 | 1–65535 |
| `mqtt_user`, `mqtt_pass` | string | | ≤ 47 / 63 chars |
| `ntp_server` | string | `pool.ntp.org` | |
| `tz` | string | | POSIX TZ |
| `ntp_offset` | int | 7200 | seconds, used only when `tz` is empty |
| `topic_prefix` | string | `edilkamin` | no `/`, `#`, `+` |
| `device_id` | string | `322707E4` (upgrades) / MAC-derived (fresh) | keep it to preserve HA entity ids; changing it moves the device in HA and wipes the old identity automatically |
| `hostname` | string | `edilkaminble` | mDNS name, `a-z 0-9 -`, 1–31 chars |
| `ha_published_prefix`, `ha_published_id` | string | | bookkeeping: identity last published to HA; do not edit |
| `feat_relax`, `feat_standby`, `feat_chrono` | bool | true | expose the switch in HA (off = removed, command refused); applied live |
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
| `web_enabled` | bool | true | local web administration on port 80; also `web_admin/set` |
| `admin_user` | string | `admin` | 1–31 chars, no `:` or `"` |
| `admin_hash` | string | | `MD5(user:Edilkamin:password)`, 32 hex; empty = locked. Set from the portal or the web page, never by hand |

Values outside their range are clamped at boot and logged
(`config corrected (n): …`).

## Runtime controls (no reboot)

* `log_level/set` – immediate; `DEBUG` and `TRACE` revert after 30 minutes.
* `bluetooth/set` – enable/disable BLE; persisted in `ble_enabled`.
* `web_admin/set` – enable/disable the web page; persisted in `web_enabled`.
* Web page *Settings* – live for Bluetooth enabled, log levels, data
  validity, command expiry, intervals, watchdogs and credentials; everything
  else is flagged *restart required*.
* `diagnostics_reset/set`, `log_dump/set`, `restart/set` – see
  [mqtt-api.md](mqtt-api.md).

## Backup and restore

*Maintenance → Export JSON* on the web page downloads every key except
`mqtt_pass` and `admin_hash`. *Import JSON* applies such a file (unknown keys
are ignored, values are validated and clamped). Re-enter the MQTT password
afterwards if the broker needs one.

## Factory reset

Hold the boot button (GPIO0) while powering the board, or use *Maintenance →
Factory reset* on the web page: `config.json`, the Wi-Fi credentials, the
administrator password and the NVS counters are erased and the portal
starts.
