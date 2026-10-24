# Troubleshooting

Everything below works from the local network, without a USB cable.

## Where to look

| Symptom | First check |
|---------|-------------|
| Device missing in Home Assistant | `availability/state` on the broker (ESP32 online?), then `homeassistant/status` birth – press *Restart ESP32* or restart HA to force a discovery republish |
| Climate shows *unavailable* | `data_valid/state`. `OFFLINE` means no valid stove answer for `data_max_age_s`. Look at `ble/state` and `last_error` |
| `ble/state` stuck on `ERROR` | `last_error`: `stove not found` (out of range / wrong MAC / stove powered off / another device holding the link), `connect failed`, `service/characteristic missing` (not an Edilkamin stove, check `stove_mac`), `connected but no valid data` (pairing lost → put the stove in pairing mode again), `link dropped` |
| Commands do nothing | `command/result`: `REJECTED` (payload invalid, value out of range, fan/power unknown yet, Bluetooth disabled), `TIMEOUT`/`FAILED` with a reason, or `VERIFIED` (then the stove did apply it) |
| Temperature not updating | `last_update/state` and `requests_ok`. The remote control reports `0` when it sleeps; the firmware skips those readings |
| Frequent reboots | `boot_count`, `reset_reason`, `last_restart_cause` in `diagnostics/state` |

## Reboot during Bluetooth initialization

An abort at `NimBLEDevice::init` / `esp_bt_controller_init` happens before
scanning or connecting to the stove. Check the framework versions first:
this firmware uses NimBLE-Arduino **1.4.3** with Arduino-ESP32 **2.0.17**
(PlatformIO `espressif32@7.0.1`, pinned in `platformio.ini`). Arduino 3.x
changes the Bluetooth integration, including startup memory reservation;
using it requires migrating the BLE code to NimBLE 2.x as well.

After restoring the pinned versions, rebuild and upload over USB:

```bash
pio run -e wemos_d1_mini32 -t clean
pio run -e wemos_d1_mini32
pio run -e wemos_d1_mini32 -t upload
pio device monitor -e wemos_d1_mini32
```

The build output should report Arduino **2.0.17** and NimBLE **1.4.3**.
Upload the firmware without erasing flash to preserve the saved settings.
If initialization still fails, capture the lines immediately before
`abort()`, especially the `ESP_ERROR_CHECK failed` error name and code.

Messages such as `No core dump partition found` or `Core dump flash config
is corrupted` concern saving diagnostics after the panic. They are separate
from the Bluetooth initialization failure; erasing settings or changing the
partition table is not needed for this dependency fix.

## Stove found, but connection fails

`init done` followed by `connecting to <MAC>` means Bluetooth initialized
and a device advertising service `abf0` was found. A later `connect failed`
does not identify the cause by itself.

If the same stove and ESP32 worked before the v1.1 upgrade, investigate the
firmware change first. The connection timeout is restored to the original
30 seconds, and the manager waits for scan completion before connecting.
The session and retry clocks use the time after blocking BLE operations.
These corrections need verification on the previously working hardware;
they do not by themselves establish the cause of every connection failure.

1. Disconnect The Mind app and temporarily disable Bluetooth on nearby
   phones or tablets that use the stove. The stove accepts one central at a time.
2. For the first connection, put the stove into Bluetooth pairing mode
   following its model-specific manual, then allow the ESP32 to retry.
3. Temporarily move the ESP32 closer to the stove. The RSSI on the
   `connecting to` line is the stove signal; the Wi-Fi RSSI is separate.
4. If it still fails, Home Assistant's *Last error* now includes the stack
   code as `BLE: connect failed (rc=<decimal>/0x<hex>)`. Capture that value,
   or capture `connect failed: rc=...` and the nearby
   `NimBLEClient` error lines from the USB serial monitor. The application
   log includes the decimal/hex stack code and its description. Some NimBLE
   early-return and timeout paths record no code; `rc=0` on a failed call
   means no stack error was recorded, not that the connection succeeded.

NimBLE error logs are enabled separately from the Arduino debug level.
They appear on USB serial; the application's failure message also enters
its normal log buffer and sinks. A matching advertisement does not prove
that the stove will accept a connection or that its BLE signal is reliable.

## Web page problems

| Symptom | Cause / fix |
|---------|-------------|
| `edilkaminble.local` does not resolve | mDNS is blocked on some networks or Windows setups; use the IP from the router or from the *Wi-Fi signal* device page in Home Assistant (`diagnostics/state` → `ip`) |
| Page loads but every action says *Set the administrator password first* | No password was ever set; the page shows the setup form on reload. If it does not, somebody already set one: factory reset (boot button) |
| Browser keeps asking for credentials | Wrong user or password. After five failures within a minute the API answers `429` for 30 s; wait and retry. The user name is case sensitive (`admin` by default) |
| Page unreachable, everything else works | `web_enabled` is `false` (switch *Web administration* in Home Assistant, or `web_admin/set ON`) |
| Settings saved but nothing changed | The answer said *Restart required*: MQTT, NTP, topics, stove MAC, BLE timings and telnet apply after a restart (banner on the Status tab) |
| Wi-Fi change failed and the module is gone | It restarted and could not join the new network: connect to `Edilkamin_BT_AP` within the three-minute portal window (repeats at every boot) and enter the right credentials |

The log (*Log* tab or `log` topic) shows `WEB: auth failed from <ip>` for
rejected logins and `configuration updated via web` for applied changes.

## Firmware update problems

| Symptom | Cause / fix |
|---------|-------------|
| *Install* button disabled, help text says *Single application slot* | The module still runs the old partition table; flash v1.4 once over USB (see [ota.md](ota.md)) |
| *current image not validated yet* | First boot after an update: wait for MQTT or 10 minutes, or restart once from the page to validate it, then upload |
| *not an Edilkamin_BT Enhanced firmware (marker missing)* | Wrong file (another project, a pre-v1.4 build, a LittleFS image) |
| *image built for another chip* | A build for ESP32-S3/C3; this board is a plain ESP32 |
| *image verification failed* | Truncated or corrupted upload; retry, check the file size against the build output |
| Page never comes back after the countdown, old version answers again | The new image crashed before validation and the bootloader rolled back; the other slot shows `aborted`. Check the serial log of the new build before retrying |
| New version runs but MQTT never connects | It will roll back only on a reset; the image validates itself after 10 minutes of uptime. Use *Roll back to previous firmware* if you prefer the old one |
| After a WebFlash the module still runs the OTA version | Old manifests did not write `boot_app0.bin`; use the current WebFlash page or `pio run -t upload`, both reset the boot slot to `app0` |

## Raise the log level remotely

1. Set the *Log level* select (or publish on `log_level/set`) to `DEBUG` or
   `TRACE`. The change applies immediately and reverts after 30 minutes.
2. Lines at or below `mqtt_log_level` (default `WARN`) are streamed on the
   `log` topic. To see everything, raise `mqtt_log_level` in the config or
   press *Dump logs to MQTT* to replay the last 48 lines of the in-memory
   buffer (which always records at the active level).
3. `TRACE` adds the raw modbus request/response bodies:

```
[   61.204] I BLE: IDLE -> SCANNING
[   62.411] I BLE: connecting to A4:C1:38:12:AB:FF
[   63.987] I BLE: GATT ready (MTU 247)
[   63.988] I BLE: CONNECTING -> CONNECTED
[   64.102] D STOVE: -> READ_MAIN_STATUS poll id=0 reg=0x02E4 val=0x0001 try=1
[   64.103] T BLE: tx pdu 010302E40001
[   64.280] T BLE: rx body 010302020263F60505
[   64.281] I BLE: CONNECTED -> COMMUNICATING
[   66.104] D STOVE: -> READ_ROOM_TEMP poll id=0 reg=0x02E9 val=0x0001 try=1
[   94.300] I BLE: session over, next in 120 s
```

`mosquitto_sub -h <broker> -t 'edilkamin/322707E4/log' -v` is enough to
follow along.

## Counter semantics

* `requests_failed` = response timeouts + invalid frames. Occasional timeouts
  at the end of a session are normal; a steady rise means a weak BLE link.
* `frames_invalid` > 0 means decryption/CRC problems: interference, or a
  stove that uses a different key (please report the model).
* `ble_disconnects` counts unexpected drops only; the voluntary disconnect
  at the end of each session is not an error.
* `queue_overflows` should stay 0; it grows only if Home Assistant floods the
  device with commands while the stove is unreachable.

## Known limitations

* The stove accepts one BLE central at a time. While the ESP32 is connected
  (30 s per session by default) the official app cannot connect; the pause
  between sessions leaves room for it. Use `bluetooth/set OFF` to hand the
  stove over to the app for longer.
* Status codes outside the known table are logged (`unknown status
  combination x/y`) and the last known status is kept. Please report them.
* Room temperatures above 25.5 °C were truncated by v0.x; this firmware
  decodes the full register and falls back to the low byte only when the
  value is implausible. Report any odd reading with the `TRACE` body.
