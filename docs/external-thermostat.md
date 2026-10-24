# External thermostat (HA05)

Optional regulation from a temperature sensor that is independent of the
stove's own remote-control probe, for example a Zigbee sensor in the room you
actually want warm:

```
Zigbee sensor → Home Assistant → MQTT → ESP32 → stove setpoint
```

Shipped since v2.0 as **Mode A** (setpoint adjustment). Mode B (external
on/off demand) was evaluated and is not shipped; see the end of this page.

## How Mode A works

The stove keeps regulating with its own probe and its own safety logic. The
ESP32 only moves the stove's **setpoint** so that the stove's internal
regulation calls for heat, or not, according to the external sensor:

| External temperature vs room target | Demand | Stove setpoint written |
|---|---|---|
| ≤ target − hysteresis | heat | stove probe + boost (default +2.0 °C), at least the target |
| ≥ target + hysteresis | idle | stove probe − relief (default −1.0 °C), at most the target |
| inside the band | unchanged | – |

Every write is clamped to the profile range (15–30 °C by default), to
±4 °C around the room target, rounded to the 0.5 °C step, rate limited (one
adjustment per `ext_min_interval_s`, default 5 min) and goes through the
normal verified command path (`command/result` shows it).

Because the stove decides itself whether to ignite, modulate, go to standby
or stop, its minimum run and cool-down cycles, its over-temperature and
pellet protections stay fully in force. The ESP32 **never sends `heat` or
`off`** for the thermostat.

## Safety behaviour

| Situation | Behaviour |
|---|---|
| ESP32 reboot | nothing happens until a fresh external measurement arrives (`waiting_sensor`) |
| Sensor silent for more than `ext_sensor_max_age_s` (default 10 min) | state `sensor_lost`; the setpoint the user had before the thermostat took over is restored once; no further writes until the sensor is back |
| Thermostat switched off | the previous setpoint is restored once |
| Home Assistant or MQTT down | no measurement → `sensor_lost` after the validity delay → previous setpoint restored; the stove runs autonomously |
| Bluetooth session not active | the adjustment waits in the command queue (user priority) and expires after `command_expiry_s` like any command |
| Measurement outside −30…60 °C or not a number | refused, logged |

While the thermostat is enabled, `target_temperature/set` from the climate
card is **rejected** with the reason *external thermostat active: use the
External setpoint entity* and the climate card snaps back to the stove's
real setpoint. The room target lives in the *External setpoint* entity.

## Home Assistant entities and topics

| Entity | Topic | Payload |
|---|---|---|
| switch *External thermostat* | `ext_thermostat/set`, `ext_thermostat/state` | `ON` / `OFF` (persisted) |
| number *External setpoint* | `ext_setpoint/set`, `ext_setpoint/state` | e.g. `20.5` (profile range, persisted) |
| number *External temperature* | `ext_temperature/set`, `ext_temperature/state` | the measurement, e.g. `19.8` (not persisted) |
| sensor *External thermostat status* | `ext_status/state` | `disabled`, `waiting_sensor`, `heat`, `idle`, `sensor_lost` |

Feed the measurement with an automation (replace the sensor and the topic):

```yaml
automation:
  - alias: Stove external thermostat feed
    trigger:
      - platform: state
        entity_id: sensor.living_room_temperature
      - platform: time_pattern
        minutes: "/4"          # keep the measurement fresh (validity 10 min)
    condition:
      - condition: template
        value_template: "{{ states('sensor.living_room_temperature') | is_number }}"
    action:
      - service: mqtt.publish
        data:
          topic: edilkamin/322707E4/ext_temperature/set
          payload: "{{ states('sensor.living_room_temperature') }}"
```

Or, without MQTT in the automation, call `number.set_value` on the *External
temperature* entity. Publishing at least every few minutes is what keeps the
sensor "valid"; a dead sensor therefore degrades safely.

Settings (web page → *External thermostat*): hysteresis, sensor validity,
minimum interval, boost and relief. The status page shows state, demand,
measurement age and the setpoint that will be restored.

## Mode B evaluation (not shipped)

Mode B would have the ESP32 send `heat` / `off` from the external sensor.
It was not retained because:

* an ignition costs several minutes of pellets and a cleaning cycle, and a
  shutdown is followed by a cool-down the stove enforces; the ESP32 cannot
  see the stove's internal timers, so any hysteresis it applies is a guess
  per model, which is exactly what the specification asks to avoid;
* a lost sensor or a Home Assistant outage would leave the stove in
  whatever state the last command put it, whereas Mode A degrades to the
  stove's own regulation;
* Mode A already covers the common case (probe in the wrong room) because
  most The Mind stoves support standby/modulation on their own setpoint.

If a stove model turns out not to modulate on its setpoint, Mode B can be
added later behind the same entities, with a per-profile validation.
