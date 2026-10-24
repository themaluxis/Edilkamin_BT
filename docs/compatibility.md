# Compatibility

| Stove | Board | Status | Notes |
|-------|-------|--------|-------|
| Edilkamin Slide2 7-UP (The Mind) | Wemos D1 Mini ESP32 | upstream reference | fan 1 only, no AirKare |
| Other "The Mind" stoves | Wemos D1 Mini ESP32 | expected to work, unverified | please report `status` codes and register behaviour |

## Capability profile

`src/stove/StoveProfile.h` holds the single *generic* profile used today:
target 15.0–30.0 °C in 0.5 steps, power P1–P5, fan 20–100 % + Auto, relax,
standby and chrono exposed; fan 2/3 and AirKare not exposed. Per-model
profiles (F08) will select entities and ranges per stove.

## Register notes (validated on the Slide2)

| Register | Use | Decoding |
|----------|-----|----------|
| `0x02E4` | main status | `(hi, lo)` table in `Decoders.cpp` |
| `0x02E9` | room temperature | tenths °C, `0` = remote asleep |
| `0x02BB` | exhaust temperature | tenths °C |
| `0x0525` | target temperature | tenths °C (write echo = applied) |
| `0x0529` | fan 1 / power | `hi` = fan (1–5, 6 = auto), `lo` = power (1–5); one write sets both |
| `0x0523` | automatic | `hi` byte |
| `0x052B` / `0x052C` | relax / standby | `0x0101` on, `0x0000` off |
| `0x0522` | chrono | `lo` byte |
| `0x02F4` (2 regs) | warning flags | second byte `0` ok, `4` pellets empty |
| `0x031C` | on/off | write `1` / `0` |

Registers listed in `src/protocol/Registers.h` under "documented but not
validated" come from the app decompilation and must be tested on hardware
before being wired to entities (alarms, AirKare, fans 2/3, chrono tables,
controller versions).

## Coexistence with the remote and the app

The stove is a BLE peripheral and accepts a single central. The original
radio remote is not affected. The phone app can only connect while the ESP32
is not (default: 120 s pause every 150 s), or when Bluetooth is disabled from
Home Assistant. Keep `ble_pause_s > 0` if you use the app.

## Pairing

The stove must be in pairing mode the first time a new central connects
(see the stove manual). Afterwards the ESP32 reconnects on its own. If every
session ends with `connected but no valid data`, redo the pairing.
