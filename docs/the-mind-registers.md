# The Mind registers: study and experimental support (F09)

This page documents what is known about the stove's Modbus registers behind
the BLE link, which ones the firmware uses, which ones are exposed as
**experimental raw sensors**, and how to validate a register on your own
stove without reflashing.

## Validated (used by the firmware)

| Register | Function | Decoding |
|---|---|---|
| `0x02E4` | main status | `(hi, lo)` table, see `Decoders.cpp`; raw word also published on `status_raw/state` so unknown combinations are never hidden |
| `0x02E9` | room temperature | tenths °C, `0` = remote asleep |
| `0x02BB` | exhaust temperature | tenths °C |
| `0x0525` | target temperature | tenths °C |
| `0x0529` | fan 1 / power | `hi` fan (1–5, 6 auto), `lo` power (1–5) |
| `0x0523` | automatic | `hi` byte |
| `0x052B` / `0x052C` | relax / standby | `0x0101` / `0x0000` |
| `0x0522` | chrono | `lo` byte |
| `0x02F4` (2) | warning flags | second byte `0` ok, `4` pellets empty |
| `0x031C` | on/off | write `1` / `0` |

## Experimental (read-only, off by default)

Enable *Experimental registers* on the web page (`exp_registers`). The poll
cycle then also reads the registers below and publishes **raw values** as
diagnostic sensors; nothing is written. Semantics are unconfirmed: compare
the values with the app and the stove display, then report them.

| Register | Sensor | What the app decompilation suggests |
|---|---|---|
| `0x02E6` | *Alarm code (exp.)* | current alarm identifier (0 = none?) |
| `0x02EC` | *Pellets remaining (exp.)* | pellet level estimate |
| `0x0032` | *Controller version (exp.)* | motherboard application version |
| `0x052A` | *Fan 2 (exp.)*, *Fan 3 (exp.)* (profiles with 2+ fans) | `hi` fan 2, `lo` fan 3, probably same scale as fan 1 |

## Register read tool

*Maintenance → Register read* on the web page, or MQTT
`register_read/set` with `0x02E6` or `0x02E6,2`:

* function 3 only (read holding registers), 1 to 3 registers (the 32-byte
  frame cannot carry more), one request every 2 s;
* queued with user priority through the normal Bluetooth session, so it may
  wait for the next session;
* result on `register/result`:
  `{"reg":"0x02E6","count":1,"values":["0x0000"]}`, in the log, in
  `command/result` (`READ_REGISTER … VERIFIED 0x02E6 = 0x0000`) and on the
  web page (*Register read* card, status refresh).

Suggested validation protocol for a register: read it in each stove state
(off, ignition, steady, shutdown), change the matching setting from the app
or the remote, read again, note the value pairs, open an issue with the
stove model.

## Not supported, and why

### AirKare

No register for AirKare has been identified. The earlier experiment in this
fork (branch `backup/old-work`) wrote `0x0522`, `0x0523` and `0x0524` with
value `1` for "on" and `0x0523` with `0` for "off". Those registers are
**chrono**, **automatic** and **temperature format**: that sequence
switched the stove to automatic mode and toggled chrono, which explains the
odd behaviour observed. The v0.x *AirKare* switch is therefore removed from
Home Assistant (v1.5) and no AirKare command exists in this firmware. If
your stove has AirKare, use the register tool while toggling it from the app
to find the real register, then report it.

### Writes to secondary fans

`0x052A` is read but never written until its semantics are confirmed on a
stove with several fans (`prof_fan_count` ≥ 2 only enables the read).

### Full chrono programming, statistics, maintenance

`0x04CE` / `0x04F8` (chrono tables) are 42 registers each: they do not fit
the 32-byte frame used by the BLE link and would need the multi-frame
protocol the app uses. `0x02C2` (stats), `0x033A`/`0x033C` (alarm log),
`0x0334` (data) and the service commands on `0x031C` (sweep, feed auger)
are documented in `src/protocol/Registers.h` and deliberately not exposed:
the specification forbids exposing service commands without validation of
their effects and protections.

## Register map reference

`src/protocol/Registers.h` lists every address recovered from the app, split
into *validated* and *documented but not validated*. Keep that split when
contributing: a register moves to the first group only with a reproducible
observation on real hardware.
