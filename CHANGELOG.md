# Changelog

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
