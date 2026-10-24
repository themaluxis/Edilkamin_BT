# Firmware updates over the network (OTA)

Since v1.4 the firmware can be updated from the web page, with two
application slots and automatic rollback. The stove is never involved: an
update only rewrites the ESP32 flash; no command is sent before, during or
after it.

## One-time prerequisite: the two-slot partition table

v0.x to v1.3 used a single application slot (`app0`, 2.3 MB) and a 1.6 MB
LittleFS. v1.4 ships a new `partitions.csv`:

| Partition | Offset | Size | Content |
|-----------|--------|------|---------|
| `nvs` | 0x9000 | 20 kB | Wi-Fi credentials, boot counter, restart cause (unchanged offset) |
| `otadata` | 0xE000 | 8 kB | which slot boots, validation state |
| `app0` (`ota_0`) | 0x10000 | 1.97 MB | application slot A |
| `app1` (`ota_1`) | 0x1F0000 | 1.97 MB | application slot B |
| `littlefs` | 0x3D0000 | 192 kB | `config.json` |

A partition table can only be changed over USB. The procedure, once:

1. On the running v1.3 page, *Maintenance → Export JSON* (keeps every setting
   except the MQTT password and the admin password hash).
2. Flash v1.4 over USB: WebFlash, or `pio run -e wemos_d1_mini32 -t upload`.
   Do **not** erase the whole flash: `nvs` keeps its offset, so the module
   rejoins your Wi-Fi by itself.
3. LittleFS moved, so the configuration is gone: open
   `http://edilkaminble.local/`, set the administrator password, then
   *Maintenance → Import JSON* with the file from step 1 and re-enter the MQTT
   password in *Settings*. Restart.

Modules provisioned directly with v1.4 or later skip this; the page's
*Firmware* card shows *OTA capable: yes* and the other slot.

Firmware size after v1.4: 1.32 MB per slot of 1.97 MB (67 %).

## Updating from the page

*Maintenance → Firmware → choose `firmware.bin` → Install* (click twice to
confirm). The upload takes 10 to 30 s; the page shows the progress, then a
countdown while the module reboots into the new image.

Checks performed before anything is committed:

| Check | Rejects |
|-------|---------|
| ESP32 image header (magic `0xE9`, chip id ESP32, application descriptor) | LittleFS images, bootloaders, images built for ESP32-S3/C3, random files |
| Project marker `EDILKAMIN_BT_FW_V1:<version>` embedded in every build of this repository | firmware from another project, pre-v1.4 builds of this project (they would not be able to update again) |
| `Update.end()` → ESP-IDF image verification (checksum, appended SHA-256) | truncated or corrupted uploads |
| Fits in the other slot | oversized images |

A refused upload leaves the running firmware untouched (`The current
firmware is untouched` on the page, reason in the answer and in the log).

The response carries the version string read from the marker, so you can
see what was written before it boots.

## Rollback

After an update the new image boots in *pending verification* state
(`ota_pending: true` in `diagnostics/state`, badge on the page). It marks
itself valid when MQTT connects, or after 10 minutes of uptime, or when you
restart it deliberately from the page or MQTT. Until then any crash,
watchdog or power cycle makes the **bootloader** return to the previous
slot automatically; nothing has to be done.

Manual rollback: *Maintenance → Roll back to previous firmware* switches the
boot slot back to the previous image if it is still valid, then restarts.
The *Firmware* card shows the other slot and its state (`valid`, `aborted`
= rolled back once, `new`, `undefined` = empty).

While an image is pending verification, a new upload is refused: wait for
MQTT or 10 minutes (or restart once to validate it).

## Recovery paths, from mildest to last resort

1. **Automatic**: the new image crashes before validation → the bootloader
   boots the previous slot at the next reset.
2. **Page reachable**: *Roll back to previous firmware*, or upload a known
   good `firmware.bin`.
3. **Page unreachable but MQTT works**: `restart/set RESTART` does not help
   (it validates the running image); use the next step.
4. **USB**: `pio run -t upload` or WebFlash. Both write `boot_app0.bin` to
   `otadata`, so the module boots `app0` again whatever slot was selected.
   The configuration survives (LittleFS is not touched).
5. **Factory reset** (boot button held at power-up) only erases settings; it
   never changes the firmware.

Keep the previous `firmware.bin` next to the new one; it is the simplest way
back when a new version boots but misbehaves.

## MQTT visibility

`diagnostics/state` publishes `fw` (running version), `ota_slot` (`app0` /
`app1`) and `ota_pending`. The *Firmware* sensor in Home Assistant therefore
shows the new version right after a successful update, and the diagnostics
document is refreshed the moment the image is validated.

## Building a firmware for OTA

```bash
pio run -e wemos_d1_mini32
ls -l .pio/build/wemos_d1_mini32/firmware.bin
```

The marker and the version come from `scripts/version.py` (`git describe`).
`build_webflash.sh` now also ships `boot_app0.bin` in the WebFlash manifest.

## Limits

* The upload is handled by the synchronous web server: the main loop is
  paused during the transfer (BLE polling and MQTT keepalive resume
  afterwards; keepalive is 60 s, transfers take 10 to 30 s).
* No signature check (secure boot is not enabled on these boards); the
  marker and digest authentication are the gate. Keep the admin password
  private.
* Downgrading to a pre-v1.4 image is refused by the marker check; use USB for
  that.
