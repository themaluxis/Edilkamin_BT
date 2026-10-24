# Web administration

Since v1.3 the ESP32 serves a local administration page at
`http://edilkaminble.local/` (mDNS) or `http://<ip>/`. It replaces the need
for a USB cable for everything except the initial flash: status, settings,
log viewer, Wi-Fi change, configuration backup, restart and factory reset.

Everything is local; the page embeds its own CSS/JS (7.8 kB gzip in flash)
and loads nothing from the Internet.

## Access and security

* **Password required.** Administration is locked until an administrator
  password is chosen: in the captive portal (*Web admin password* field) or,
  on first access to the page, through the *Set the administrator password*
  form. The user name defaults to `admin`.
* **Digest authentication** (RFC 2617, MD5, `qop=auth`). The browser prompts
  once per session; the password never travels in clear and only its hash
  `MD5(user:Edilkamin:password)` is stored in `config.json` (`admin_hash`).
  Changing the user name therefore requires entering a password again.
* **Brute-force throttle**: five failures within a minute lock the API for
  30 s (HTTP 429). Failures are logged with the client IP and counted
  (`web_auth_failures` on the status page).
* **No secrets leave the device**: the settings page never shows the MQTT
  password (only whether one is set), the export omits `mqtt_pass` and
  `admin_hash`, and an import ignores them.
* **Can be switched off entirely** (`web_enabled`, Home Assistant switch
  *Web administration*, or `web_admin/set OFF`). Re-enable it with the same
  switch or from the captive portal after a factory reset.
* The server is plain HTTP on port 80, intended for the LAN only. Do not
  expose it to the Internet; use a VPN if you need remote access.
* The first password set wins. If you open the page and the setup form is
  already gone although you never set a password, someone else on your
  network did: factory-reset the module (boot button) and set it again.

## Pages

| Tab | Content |
|-----|---------|
| Status | stove values (status, temperatures, power/fan, modes, pellets, last valid answer), connectivity (Wi-Fi, MQTT, BLE, last error), system (firmware, uptime, heap, time, boots, log level), counters; refreshed every 5 s |
| Settings | every key of [configuration.md](configuration.md) grouped by theme; the response says what was corrected (clamped) and whether a restart is needed |
| Wi-Fi | current network and a scan-and-change form |
| Log | the in-memory log buffer (48 lines), level selector (`DEBUG`/`TRACE` are temporary, 30 min), auto-refresh, download |
| Maintenance | export / import JSON, restart, factory reset (type `ERASE`), firmware and partition information (OTA readiness) |

### Which settings apply live

Applied immediately: Bluetooth enabled, log levels, data validity, command
expiry, diagnostics interval, watchdog delays, administrator credentials,
web enabled. Everything else (MQTT broker, NTP/timezone, topics, stove MAC,
BLE timings, telnet) is saved and flagged **Restart required**; the status
page shows a banner with a restart button until the ESP32 reboots.

### Changing Wi-Fi

The new credentials are written to the Wi-Fi driver storage and the module
restarts. If the new network cannot be joined, the captive portal
`Edilkamin_BT_AP` opens for three minutes at each boot (then the firmware
keeps retrying and restarts after `wifi_reboot_after_min`), so a typo never
bricks the module.

## HTTP API

All responses are JSON unless noted. Authenticated endpoints answer `401`
with a `WWW-Authenticate: Digest …` header, `403 {"error":"setup_required"}`
while no password is set, and `429` while throttled.

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| GET | `/` | no | the page (gzip, ETag) |
| GET | `/api/bootstrap` | no | `{setup_required, fw, realm, log_level, log_level_temporary}` |
| POST | `/api/setup` | no, only while unset | `{user, password}` → sets the first administrator password |
| GET | `/api/status` | yes | system / wifi / mqtt / ble / stove / counters document |
| GET | `/api/config` | yes | configuration without secrets + `has_mqtt_pass`, `restart_pending` |
| POST | `/api/config` | yes | partial update; `mqtt_pass` / `admin_pass` accepted as plaintext and hashed/stored server-side; answers `{applied, ignored, unknown, fixes, notes, restart_required, web_stopping}` |
| GET | `/api/config/export` | yes | download `edilkamin-config.json` (no secrets) |
| GET | `/api/log` | yes | `text/plain`, oldest line first |
| POST | `/api/log/level` | yes | `{"level":"DEBUG"}` |
| POST | `/api/restart` | yes | restarts 0.5 s after the answer (`last_restart_cause = web restart`) |
| POST | `/api/factory-reset` | yes | `{"confirm":"ERASE"}` |
| POST | `/api/wifi` | yes | `{"ssid","password"}` then restart |
| GET | `/api/wifi/scan` | yes | `{"status":"scanning"}` until `{"status":"done","networks":[…]}` |

Example with curl (digest):

```bash
curl --digest -u admin http://edilkaminble.local/api/status
curl --digest -u admin -H 'Content-Type: application/json' \
     -d '{"log_level":4}' http://edilkaminble.local/api/config
curl --digest -u admin -o backup.json http://edilkaminble.local/api/config/export
```

Request bodies are limited to 6 kB. The server is single-threaded and
synchronous: a request blocks the main loop for a few milliseconds, which is
harmless for the BLE and MQTT state machines (they are mailbox driven).

## Lost password

Hold the boot button (GPIO0) while powering the module: configuration,
Wi-Fi credentials and the password are erased and the setup portal starts.
The export file from the Maintenance tab restores every other setting.
