# Architecture

The firmware is split into small modules with one responsibility each. Pure
modules (no Arduino / NimBLE / MQTT dependency) are compiled and unit tested
on the host (`pio test -e native`); the platform layer only wires them to the
ESP32 libraries.

```
src/
  main.cpp                     wiring: setup(), loop(), MQTT/BLE callbacks
  core/        Log.h           logging facade usable everywhere
               Strings.h       bounded string helpers, FNV hash
  protocol/    ModbusFrame     32-byte frame layout, CRC16, PKCS#7, parsing   [pure]
               Decoders        register values -> typed stove values         [pure]
               Registers.h     register map ("The Mind")                    [pure]
               Crypto          AES-128-CBC (mbedtls)                         [esp32]
  commands/    Command         command model + lifecycle states              [pure]
               CommandQueue    priority queue, de-dup, overflow accounting   [pure]
               CommandFactory  MQTT payload validation -> Command            [pure]
  stove/       StoveState      confirmed state + freshness                   [pure]
               StoveProfile    capabilities (ranges, optional functions)     [pure]
               StoveController command lifecycle, polling, verification     [pure]
  ble/         BleSessionPolicy explicit BLE state machine                   [pure]
               BleManager      NimBLE glue (scan, connect, GATT, notify)     [esp32]
  mqtt/        Topics          topic builder/parser                          [pure]
               MqttManager     PubSubClient wrapper: LWT, backoff, de-dup    [esp32]
               Discovery       Home Assistant discovery documents            [esp32]
  logging/     Logger          levels, ring buffer, sinks, auto-revert       [pure]
  config/      AppConfig       schema v2 + validation                        [pure]
               ConfigStore     LittleFS JSON persistence + migration         [esp32]
  diagnostics/ Diagnostics     diagnostics JSON, last-error tracker          [pure]
test/          one directory per module (Unity, native)
```

## Data flow

```
 Home Assistant ──MQTT──▶ MqttManager ──▶ main: onMqttMessage
                                             │  parseSet(topic) → entity
                                             ▼
                                   StoveController.handleUserCommand
                                             │  CommandFactory (validate)
                                             │  CommandQueue (priority/de-dup)
                                             │  events.onUserCommandQueued → BleManager.requestSession
                                             ▼
                  BleSessionPolicy ◀── tick ── BleManager.loop ── sendPdu ──▶ stove (AES frame)
                        │ state changes                 ▲
                        ▼                               │ notify (AES frame)
                 main: onBleState ──▶ controller.setLinkReady / MQTT ble/* topics
                                                        ▼
                                   StoveController.onFrame (CRC, match in-flight)
                                             │  Decoders → StoveState (confirmed values)
                                             ▼
                       events.onObservedChanged / onCommandResult / onDataValidChanged
                                             ▼
                                MqttManager.publish (only when the value changed)
```

## Principles

* **One source of truth** – `StoveState` only contains values read back from
  the stove (or echoed by it). Requested values live in the command queue
  until confirmed; Home Assistant never sees an "optimistic" state.
* **One owner for the BLE link** – `BleSessionPolicy` decides, `BleManager`
  executes. NimBLE callbacks run on the BLE host task and only fill
  mailboxes; everything else happens on the Arduino task.
* **Every command has an observable outcome** – see
  [mqtt-api.md](mqtt-api.md#command-results).
* **Safety first** – writes are never re-sent blindly. A write whose echo was
  lost is followed by a read that decides between `VERIFIED`, `FAILED` or
  (for ignition) `TIMEOUT` + status refresh. Retained MQTT commands are
  deleted before subscribing so a reboot can never replay an old `heat`.
* **No reboot loops** – losing MQTT, Wi-Fi or the stove is a *state*, not a
  reason to restart. Optional watchdogs (`wifi_reboot_after_min`,
  `ble_reboot_after_min`) are long, off by default for BLE, and record their
  cause in NVS so it shows up in the diagnostics after the reboot.

## BLE session state machine

```
            enable                 due / user cmd              stove found
 DISABLED ─────────▶ IDLE ──────────────────────▶ SCANNING ───────────────▶ CONNECTING
    ▲                 ▲                              │ scan timeout            │ GATT ok / fail
    │ disable (any)   │ session over                 ▼                         ▼
    └────────────── COMMUNICATING ◀── 1st valid ── CONNECTED ──(no data 15 s)──▶ ERROR
                       │ link dropped                                            │ backoff 5 s → 120 s
                       └────────────────────────────────────────────────────────▶ │
                                                                                  └──▶ SCANNING
```

* A session lasts `ble_session_s` (30 s) and is followed by a pause of
  `ble_pause_s` (120 s). The pause lets the original remote / phone app talk
  to the stove. `ble_pause_s = 0` keeps the link up permanently.
* A user command shortens the pause (min 3 s gap) but never bypasses the
  error backoff, so a stove that is out of reach does not trigger connection
  storms.
* Only transitions are published (`ble/state`, `ble/connected`).

## Command lifecycle

```
RECEIVED → VALIDATED → QUEUED → SENT → ACKNOWLEDGED → VERIFIED
                │          │       │          │
                ▼          ▼       ▼          ▼
            REJECTED    FAILED  TIMEOUT    FAILED (read-back differs)
```

| Kind | Retry on timeout | Verification |
|------|------------------|--------------|
| Poll read | once | – |
| Write (temperature, fan/power, auto, relax, standby, chrono) | never | read the register back, compare |
| Write on/off | never | read the status; `VERIFIED` when the status is conclusive, otherwise the echo stands |

Queue rules (`CommandQueue`): capacity 16; priority User > Verify > Poll;
a newer write to the same register supersedes a pending one (the old one is
reported `REJECTED superseded`); identical poll reads collapse; when full, a
user command evicts the oldest poll read, otherwise it is reported `FAILED
command queue full`. User/verify commands expire after `command_expiry_s`.
