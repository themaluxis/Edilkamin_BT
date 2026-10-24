# Installation

## 1. Flash

* **WebFlash** (Wemos D1 Mini ESP32 only):
  <https://netmb.github.io/Edilkamin_BT/webflash/> – hold the boot button
  while plugging the USB cable, then follow the browser dialog.
* **From source**:

  ```bash
  pip install platformio          # or use the VS Code extension
  pio run -e wemos_d1_mini32      # build
  pio run -e wemos_d1_mini32 -t upload
  pio device monitor              # 115200 baud
  ```

  The firmware version embedded in the binary comes from `git describe`
  (`scripts/version.py`).

## 2. First configuration

1. Join the Wi-Fi network `Edilkamin_BT_AP` and open the captive portal.
2. Enter your Wi-Fi, MQTT broker, NTP server, timezone, (optionally) the
   stove's BLE MAC address and a **web admin password** (8+ characters).
   See [configuration.md](configuration.md).
3. Put the stove in pairing mode the first time (as with the official app).
4. The ESP32 reboots, connects, and Home Assistant discovers a device named
   *Edilkamin* with the climate entity plus diagnostics.
5. Open `http://edilkaminble.local/` (or the IP shown by your router) and log
   in as `admin`. If you skipped the password in the portal, the page asks
   you to set one first. See [web-ui.md](web-ui.md).

If the Wi-Fi cannot be joined the portal stays up for 3 minutes, after which
the firmware keeps running and retries the network every 30 s (restart after
`wifi_reboot_after_min`, default 15 min, so a router reboot does not cause a
restart loop).

## 3. Verify

* `Stove connected` turns on during BLE sessions (30 s every 2.5 min by
  default) and `Stove data fresh` stays on between sessions.
* `Bluetooth state` cycles `IDLE → SCANNING → CONNECTING → CONNECTED →
  COMMUNICATING → IDLE`.
* Set a temperature: `command/result` goes `QUEUED → SENT → ACKNOWLEDGED →
  VERIFIED` within a few seconds.

## 4. Updating

* **From v1.4 on**: *Maintenance → Firmware → Install* on the web page with
  the new `firmware.bin`. Two slots, automatic rollback, see [ota.md](ota.md).
* **From v0.x–v1.3 to v1.4**: one last USB flash installs the two-slot
  partition table. Export the configuration first, flash without erasing
  (Wi-Fi credentials survive), set the admin password, import. Details in
  [ota.md](ota.md#one-time-prerequisite-the-two-slot-partition-table).
* USB (WebFlash or `pio run -t upload`) always remains possible and resets
  the boot slot to `app0`.

## Running the unit tests

```bash
pio test -e native
```

Builds the pure modules with the host compiler and runs the Unity suites in
`test/` (protocol framing, decoders, command queue/factory, BLE state machine,
controller lifecycle, configuration and its JSON codec, logger, diagnostics
JSON, MD5 / HTTP digest authentication, login throttle, OTA image header and
marker scanner).
