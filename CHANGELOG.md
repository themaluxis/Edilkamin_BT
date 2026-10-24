# Changelog

## Unreleased — v2.0 "Advanced Control" (HA05 / F08 / F09 of the Enhanced spec)

### Added
- External thermostat, mode A: regulates from a Home Assistant sensor by moving the stove setpoint only (never `heat`/`off`), with hysteresis, profile and ±4 °C clamps, rate limit, sensor validity with one-shot restore of the previous setpoint, no action after a reboot until a fresh measurement. Entities: switch, external setpoint and external temperature numbers, status sensor. `docs/external-thermostat.md` includes the automation example and the Mode B evaluation.
- Stove profiles: `generic` (validated) or `custom` (setpoint range, power/fan levels, fan auto, 1–3 fans), applied live to the climate card, the Power select, the thermostat limits and command validation.
- Experimental registers (`exp_registers`, off by default): alarm `0x02E6`, pellets remaining `0x02EC`, controller version `0x0032`, fans 2/3 `0x052A` as raw diagnostic sensors. `status_raw` sensor always published so unknown status words are visible.
- Diagnostic register read tool (web page and `register_read/set`): function 3 only, 1–3 registers, rate limited, result on `register/result`.
- `docs/the-mind-registers.md`: validated / experimental / unsupported registers, including why the old AirKare experiment toggled chrono and automatic instead.
- Schema v5; 76 native tests (thermostat, profiles, register tool, experimental polling).

## Unreleased — v1.5 "Home Assistant" (HA02/HA03 of the Enhanced spec)

### Added
- Stable identity: fresh modules get a `device_id` derived from the ESP32 MAC; upgraded modules keep their stored id. Changing `device_id` or `topic_prefix` wipes the previous identity from Home Assistant (discovery documents and retained states) on the next MQTT connection (`ha_published_prefix` / `ha_published_id` bookkeeping).
- Device document: `configuration_url` (web page), `connections` (ESP32 MAC, stove Bluetooth address), `hw_version`.
- *Power* select entity (`power/set` P1–P5, `power/state`) next to the climate presets.
- Per-module stove features `feat_relax`, `feat_standby`, `feat_chrono`: the switch is removed from Home Assistant and the command refused when off; applied live from the web page.
- Clean-up of obsolete discovery documents (v0.x *AirKare* switch, disabled feature switches); *Republish discovery* button (`discovery_republish/set`).
- Configurable `hostname` (mDNS); schema v4; tests for the power entity and hostname normalisation (65 in total).

## Unreleased — v1.4 "OTA" (F06 of the Enhanced spec)

### Added
- Two-slot partition table (`app0`/`app1` 1.97 MB each, 192 kB LittleFS, `nvs` offset unchanged). **Requires one USB flash**; see `docs/ota.md` for the export → flash → import procedure (Wi-Fi credentials survive).
- Firmware upload from the web page (*Maintenance → Firmware*): ESP32 header check, embedded project marker `EDILKAMIN_BT_FW_V1:<version>`, ESP-IDF image verification, slot size check; the running firmware is untouched when a check fails. Response reports size and version; the module restarts into the new slot.
- Automatic rollback: the new image boots in pending-verification state and validates itself on MQTT connection, after 10 minutes, or on an intentional restart; any crash or reset before that makes the bootloader return to the previous slot. Manual rollback button and `/api/update/rollback`.
- `/api/update/info`; `ota_slot` and `ota_pending` in `diagnostics/state`; diagnostics refreshed when an image gets validated.
- `docs/ota.md`, OTA scenarios in `docs/validation.md`, native tests for the image header check and the marker scanner (64 tests in total).

### Changed
- `build_webflash.sh` ships `boot_app0.bin` at 0xE000 so a WebFlash always boots `app0` again, even after OTA updates ran from `app1`.
- Firmware size 1 316 161 bytes (66.9 % of a 1.97 MB slot).

## Unreleased — v1.3 "Management" (F05 of the Enhanced spec)

### Added
- Local web administration at `http://edilkaminble.local/` (mDNS) served from flash: status (stove, connectivity, system, counters), full settings editor with live apply / restart-required reporting, Wi-Fi scan and change, log viewer with level selector and download, configuration export/import (no secrets), restart, factory reset with typed confirmation, firmware and partition information.
- HTTP Digest authentication (RFC 2617); only `MD5(user:realm:password)` is stored (`admin_user`, `admin_hash`). Administration stays locked until a password is set (captive portal field or first-access form). Five failed logins per minute lock the API for 30 s.
- `web_enabled` setting, `web_admin/set|state` topics and a *Web administration* switch in Home Assistant to disable the service entirely.
- Configuration schema v3 with a shared JSON codec (`config/ConfigJson`), used by the LittleFS store, the web API and the export/import; migration from v1/v2 is automatic.
- Native tests for MD5, digest parsing/verification (RFC vector), nonce slots, login throttle and the config codec (60 tests in total).
- `docs/web-ui.md`; web scenarios in `docs/validation.md`.

### Changed
- Captive portal: password fields are no longer pre-filled with stored values (empty keeps the current one); new *Web admin password* field.
- `last_restart_cause` gains `web restart` and `wifi credentials changed`.
- Firmware size 1 307 677 bytes (53.9 % of the application slot).

## Unreleased — v1.1/v1.2 "Reliability & Diagnostics" (P0 scope of the Enhanced spec)

### Fixed
- Keep NimBLE 1.4.3 on Arduino-ESP32 2.0.17 (`espressif32@7.0.1`) and explicitly build with C++17.
- Restore the original 30-second BLE connection timeout and wait for scan completion before consuming a discovered stove across tasks.
- Start connection/session timers after blocking connection and GATT operations; refresh the main-loop clock afterwards.
- Include the BLE connection stack error code in Home Assistant's last-error diagnostic, alongside the detailed USB/MQTT application log.

### Added
- Explicit BLE session state machine (`DISABLED/IDLE/SCANNING/CONNECTING/CONNECTED/COMMUNICATING/ERROR`) with exponential backoff, configurable session/pause, optional target MAC.
- Command lifecycle with observable results on `command/result` (`QUEUED → SENT → ACKNOWLEDGED → VERIFIED`, or `REJECTED/TIMEOUT/FAILED` with a reason); priority queue with de-duplication, overflow accounting and expiry.
- Frame validation: fixed key, PKCS#7 padding, CRC16, function code, in-flight matching, modbus exceptions.
- Read-back verification after every write; writes are never re-sent blindly.
- Three-level availability: MQTT Last Will (`availability/state`), `ble/connected`, `data_valid/state` + `last_update/state`; stale data makes stove entities unavailable.
- Runtime log levels (`log_level/set`, HA select) with auto-revert, in-memory ring buffer, MQTT log stream and dump, Telnet opt-in.
- Diagnostics document and 19 diagnostic/config entities (RSSI, counters, uptime, heap, boot count, reset reason, last error, queue, firmware version…), reset button, restart button.
- Configuration schema v2 (validated, migrated from v0.x), POSIX timezone with DST, stove MAC and BLE timings in the portal.
- Room temperature sensor, `hvac_action`, device info (manufacturer/model/sw_version).
- Native unit tests (`pio test -e native`), 51 test cases.
- Documentation set under `docs/` and a spec gap analysis.

### Changed
- `bluetooth/state` means "BLE enabled" (persisted) and is published on change only.
- Discovery documents no longer use `optimistic` nor `retain: true`; retained commands are cleared before subscribing.
- Fan and power are read with a single register read.
- Temperature commands are parsed numerically (`17` is 17.0 °C, not 1.7 °C).
- No restart on MQTT loss; Wi-Fi watchdog 15 min (configurable); BLE watchdog off by default.
- `platformio.ini` pins `espressif32@7.0.1`; firmware version injected from `git describe`.

### Removed
- `settings.ini.dist` (compile-time credentials), `Helper.h/.cpp` (replaced by `protocol/`).

### Safety
- Retained `hvac_mode/set` messages can no longer ignite the stove after a reboot.
- Ignition/shutdown orders are never automatically re-sent; a lost echo triggers a status read instead.
