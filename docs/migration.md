# Migrating from v0.x

## What stays the same

* MQTT topic layout `edilkamin/322707E4/<entity>/(state|set)`.
* Home Assistant discovery topics and `unique_id`s → entity ids are kept.
* The captive portal, the WebFlash procedure and the Wemos D1 Mini ESP32 as
  reference board.
* `config.json` is read and migrated (schema 1 → 3); no re-configuration
  needed. v1.3 adds `web_enabled`, `admin_user`, `admin_hash` with defaults.

## What changes in Home Assistant

| Before (v0.x) | Now |
|---------------|-----|
| `availability/state` meant "BLE connected at least once" and stayed `ONLINE` for ever | `availability/state` is the ESP32 Last Will. Stove entities additionally require `data_valid/state`, so a stove that stopped answering becomes *unavailable* after `data_max_age_s` (6 min) instead of showing old values |
| Entities were `optimistic`: toggles flipped immediately even if the stove never got the command | No optimistic mode. A switch flips back until the stove echoes the write (seconds while connected, up to a few seconds plus connection time otherwise). `command/result` tells why when it does not |
| Discovery set `retain: true` on command topics, so the broker replayed `heat`/`off` to the ESP32 after every reboot | Commands are not retained; retained commands are deleted before subscribing |
| `bluetooth/state` was published every loop iteration | Only on change. The switch now means "Bluetooth enabled" and is persisted |
| Temperature `17` was interpreted as 1.7 °C (decimal point removal) | Strict numeric parsing: `17`, `17.0`, `17.5` all mean 17.x °C; invalid or out-of-range values are rejected |
| Power/fan commands sent without feedback | Every write is echoed, read back and reported (`VERIFIED`/`FAILED`) |
| ESP32 restarted after 30 s without MQTT and after 5 min without BLE (even when BLE was switched off) | No restart on MQTT loss; BLE loss is a state (`ERROR` + backoff). Optional watchdogs: Wi-Fi 15 min, BLE off by default |
| Debug level fixed at compile time (serial or telnet) | Runtime log level (select entity / `log_level/set`), MQTT log stream, in-memory buffer dump. Telnet is off by default |

New entities appear automatically (room temperature sensor, diagnostics,
buttons, log level select, *Web administration* switch). If Home Assistant still shows duplicated or
orphaned entities from older experiments, delete the device in *MQTT →
devices*; it is recreated on the next discovery publish.

## Timezone

v0.x used a fixed `ntp_offset` (7200 s). It is still honoured when `tz` is
empty; set `tz` (portal field *Timezone*) to get automatic daylight saving.

## Build system

`settings.ini.dist` (compile-time Wi-Fi/MQTT credentials) is gone; everything
is configured at runtime. `platformio.ini` pins `espressif32@7.0.1`
(Arduino-ESP32 2.0.17) with NimBLE 1.4.3, uses C++17, and adds a `native`
environment for unit tests. Arduino 3.x requires a separate NimBLE 2.x
migration; simply upgrading the platform is not supported.

## After upgrading to v1.3

Open `http://edilkaminble.local/`: the page asks for an administrator
password on first access (the portal is not shown again when the Wi-Fi still
connects). Until a password is set the page is read-only locked and the
firmware works exactly as before. See [web-ui.md](web-ui.md).

## Roadmap for the next releases

* **v1.3 Management** – delivered: local web UI with digest authentication,
  full settings editor, log viewer, Wi-Fi change, export/import, restart,
  factory reset.
* **v1.4 OTA** – web upload + rollback. Requires a two-slot partition table;
  the current firmware (1.31 MB on Arduino 2.0.17) fits a 2 × 0x1E0000 layout
  on 4 MB flash with a 192 kB LittleFS, see `docs/spec-gap-analysis.md`. The
  v1.3 export/import is the migration path for that one-time USB flash.
* **v1.5 Home Assistant** – identity from the stove serial / ESP MAC with a
  migration path for `322707E4`, discovery clean-up of obsolete documents.
* **v2.0** – external thermostat, stove profiles, extra The Mind functions.
