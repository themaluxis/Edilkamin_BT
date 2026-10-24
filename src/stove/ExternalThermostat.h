// External-sensor thermostat, "Mode A" of HA05: the stove keeps regulating on
// its own probe; this module only moves the stove's setpoint so that a room
// measured by an independent Home Assistant sensor reaches a target.
//
// Safety properties (spec §8):
//  * never sends ignition/shutdown; only setpoint writes within the profile
//    range and within ±maxOffset of the room target;
//  * does nothing until a fresh external measurement has been received
//    (so a reboot never changes the stove);
//  * writes are rate limited (minAdjustIntervalMs) and go through the normal
//    verified command path;
//  * when the sensor goes stale it restores the setpoint the user had before
//    the thermostat took over, once, then waits.
// Pure C++, unit tested natively.
#pragma once
#include <cstdint>

namespace edk {

struct ExtThermostatConfig {
  bool enabled = false;
  int16_t setpointTenths = 200;          // room target (external sensor)
  int16_t hysteresisTenths = 5;          // ±0.5 °C band around the target
  uint32_t sensorMaxAgeMs = 600000;      // measurement older than this = lost
  uint32_t minAdjustIntervalMs = 300000; // between two stove setpoint writes
  int16_t boostTenths = 20;              // stove setpoint above its probe when heating
  int16_t reliefTenths = 10;             // stove setpoint below its probe when idle
  int16_t maxOffsetTenths = 40;          // never further than this from the target
  int16_t minTenths = 150;               // stove profile limits
  int16_t maxTenths = 300;
  int16_t stepTenths = 5;
};

enum class ExtDemand : uint8_t { Unknown = 0, Heat, Idle };
enum class ExtState : uint8_t { Disabled = 0, WaitingSensor, Regulating, SensorLost };

const char *extDemandName(ExtDemand d);
const char *extStateName(ExtState s);

struct ExtInputs {
  bool stoveRoomKnown = false;   // the stove's own probe (last known value)
  int16_t stoveRoomTenths = 0;
  bool stoveSetpointKnown = false;
  int16_t stoveSetpointTenths = 0;
};

struct ExtAction {
  bool write = false;        // true: write setpointTenths to the stove
  int16_t setpointTenths = 0;
  const char *reason = "";
};

class ExternalThermostat {
public:
  void configure(const ExtThermostatConfig &cfg);
  const ExtThermostatConfig &config() const { return cfg_; }

  void setEnabled(bool on, uint32_t now);
  void setSetpoint(int16_t tenths) { cfg_.setpointTenths = tenths; }
  // Measurement from Home Assistant (validated by the caller: -30..60 °C).
  void onExternalTemp(int16_t tenths, uint32_t now);

  // Periodic evaluation. Returns the action to perform (at most one write).
  ExtAction tick(uint32_t now, const ExtInputs &in);

  ExtState state() const { return state_; }
  ExtDemand demand() const { return demand_; }
  bool sensorValid(uint32_t now) const {
    return haveExt_ && (uint32_t)(now - extAt_) <= cfg_.sensorMaxAgeMs;
  }
  bool haveExternal() const { return haveExt_; }
  int16_t externalTenths() const { return extTenths_; }
  uint32_t externalAgeMs(uint32_t now) const { return haveExt_ ? now - extAt_ : 0; }
  bool baseKnown() const { return baseKnown_; }
  int16_t baseTenths() const { return baseTenths_; }
  uint32_t lastWriteAt() const { return lastWriteAt_; }
  // The stove accepted (or refused) our last write; keeps the rate limiter honest.
  void onWriteSettled(uint32_t now) { lastWriteAt_ = now ? now : 1; }

private:
  int16_t clampStep(int16_t v) const;
  ExtThermostatConfig cfg_;
  ExtState state_ = ExtState::Disabled;
  ExtDemand demand_ = ExtDemand::Unknown;
  bool haveExt_ = false;
  int16_t extTenths_ = 0;
  uint32_t extAt_ = 0;
  bool baseKnown_ = false;
  int16_t baseTenths_ = 0;
  bool restorePending_ = false;
  const char *restoreReason_ = "";
  uint32_t lastWriteAt_ = 0;
  bool wroteOnce_ = false;
};

} // namespace edk
