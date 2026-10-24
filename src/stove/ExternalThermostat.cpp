#include "ExternalThermostat.h"

namespace edk {

const char *extDemandName(ExtDemand d) {
  switch (d) {
  case ExtDemand::Heat:
    return "heat";
  case ExtDemand::Idle:
    return "idle";
  default:
    return "unknown";
  }
}

const char *extStateName(ExtState s) {
  switch (s) {
  case ExtState::Disabled:
    return "disabled";
  case ExtState::WaitingSensor:
    return "waiting_sensor";
  case ExtState::Regulating:
    return "regulating";
  case ExtState::SensorLost:
    return "sensor_lost";
  }
  return "?";
}

void ExternalThermostat::configure(const ExtThermostatConfig &cfg) {
  bool wasEnabled = cfg_.enabled;
  cfg_ = cfg;
  if (cfg_.hysteresisTenths < 1)
    cfg_.hysteresisTenths = 1;
  if (cfg_.stepTenths < 1)
    cfg_.stepTenths = 1;
  if (cfg_.enabled != wasEnabled)
    setEnabled(cfg_.enabled, 0);
  else if (!cfg_.enabled)
    state_ = ExtState::Disabled;
}

void ExternalThermostat::setEnabled(bool on, uint32_t now) {
  (void)now;
  cfg_.enabled = on;
  if (on) {
    state_ = ExtState::WaitingSensor;
    demand_ = ExtDemand::Unknown;
    baseKnown_ = false; // captured from the stove at the first regulation step
    wroteOnce_ = false;
    restorePending_ = false;
  } else {
    if (state_ == ExtState::Regulating || state_ == ExtState::SensorLost) {
      if (baseKnown_ && wroteOnce_) {
        restorePending_ = true;
        restoreReason_ = "thermostat disabled: restoring previous setpoint";
      }
    }
    state_ = ExtState::Disabled;
    demand_ = ExtDemand::Unknown;
  }
}

void ExternalThermostat::onExternalTemp(int16_t tenths, uint32_t now) {
  haveExt_ = true;
  extTenths_ = tenths;
  extAt_ = now;
}

int16_t ExternalThermostat::clampStep(int16_t v) const {
  int16_t lo = cfg_.setpointTenths - cfg_.maxOffsetTenths;
  int16_t hi = cfg_.setpointTenths + cfg_.maxOffsetTenths;
  if (v < lo)
    v = lo;
  if (v > hi)
    v = hi;
  if (v < cfg_.minTenths)
    v = cfg_.minTenths;
  if (v > cfg_.maxTenths)
    v = cfg_.maxTenths;
  int16_t step = cfg_.stepTenths;
  int16_t r = (int16_t)(((v + (step / 2)) / step) * step);
  if (r > cfg_.maxTenths)
    r = (int16_t)(cfg_.maxTenths - (cfg_.maxTenths % step));
  if (r < cfg_.minTenths)
    r = (int16_t)(cfg_.minTenths + ((step - cfg_.minTenths % step) % step));
  return r;
}

ExtAction ExternalThermostat::tick(uint32_t now, const ExtInputs &in) {
  ExtAction a;
  // A pending restore (disable or sensor loss) is the only write allowed
  // outside regulation, and it only brings back the user's own setpoint.
  if (restorePending_) {
    if (!in.stoveSetpointKnown || in.stoveSetpointTenths != baseTenths_) {
      restorePending_ = false;
      a.write = true;
      a.setpointTenths = baseTenths_;
      a.reason = restoreReason_;
      lastWriteAt_ = now ? now : 1;
      return a;
    }
    restorePending_ = false; // already at base
  }
  if (!cfg_.enabled) {
    state_ = ExtState::Disabled;
    return a;
  }
  if (!sensorValid(now)) {
    if (state_ == ExtState::Regulating) {
      state_ = ExtState::SensorLost;
      demand_ = ExtDemand::Unknown;
      if (baseKnown_ && wroteOnce_) {
        restorePending_ = true;
        restoreReason_ = "external sensor lost: restoring previous setpoint";
        return tick(now, in); // performs the restore above
      }
    } else if (state_ != ExtState::SensorLost) {
      state_ = ExtState::WaitingSensor;
    }
    return a;
  }

  // Sensor fresh: regulate.
  if (state_ != ExtState::Regulating) {
    state_ = ExtState::Regulating;
    if (!baseKnown_ && in.stoveSetpointKnown) {
      baseKnown_ = true;
      baseTenths_ = in.stoveSetpointTenths;
    }
  }
  const int16_t target = cfg_.setpointTenths;
  const int16_t h = cfg_.hysteresisTenths;
  if (extTenths_ <= target - h)
    demand_ = ExtDemand::Heat;
  else if (extTenths_ >= target + h)
    demand_ = ExtDemand::Idle;
  else if (demand_ == ExtDemand::Unknown)
    demand_ = extTenths_ < target ? ExtDemand::Heat : ExtDemand::Idle;

  // Desired stove setpoint relative to the stove's own probe so that the
  // stove's internal regulation calls for heat (or not).
  int16_t desired;
  if (demand_ == ExtDemand::Heat) {
    desired = in.stoveRoomKnown ? (int16_t)(in.stoveRoomTenths + cfg_.boostTenths)
                                : (int16_t)(target + cfg_.boostTenths);
    if (desired < target)
      desired = target;
  } else {
    desired = in.stoveRoomKnown ? (int16_t)(in.stoveRoomTenths - cfg_.reliefTenths)
                                : (int16_t)(target - cfg_.reliefTenths);
    if (desired > target)
      desired = target;
  }
  desired = clampStep(desired);

  if (in.stoveSetpointKnown && in.stoveSetpointTenths == desired)
    return a; // nothing to change
  if (lastWriteAt_ != 0 &&
      (uint32_t)(now - lastWriteAt_) < cfg_.minAdjustIntervalMs)
    return a; // rate limited
  a.write = true;
  a.setpointTenths = desired;
  a.reason = demand_ == ExtDemand::Heat ? "external thermostat: heat demand"
                                        : "external thermostat: no heat demand";
  lastWriteAt_ = now ? now : 1;
  wroteOnce_ = true;
  return a;
}

} // namespace edk
