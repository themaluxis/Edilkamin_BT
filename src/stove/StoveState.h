// Single source of truth for the *confirmed* state of the stove.
// Only values that were read back from (or echoed by) the stove end up here;
// requested-but-unconfirmed values live in the command queue.
#pragma once
#include <cstdint>

namespace edk {

enum class StoveStatus : uint8_t {
  Unknown = 0,
  Off,
  Cleaning,
  LoadingPellets,
  IgnitionChargeSuspend,
  CheckExhaustTemp,
  ThresholdTempCheck,
  Warmup,
  Ok,
  Shutdown,
  Extinction,
};

const char *stoveStatusLabel(StoveStatus s); // labels kept from v0.x for HA

enum class PelletLevel : uint8_t { Unknown = 0, Ok, Empty };

// Which observed value changed (used for selective MQTT publishing).
enum class Observable : uint8_t {
  RoomTemp = 0,
  TargetTemp,
  Thermocouple,
  Status,
  Fan1,
  Power,
  Automatic,
  Relax,
  Standby,
  Chrono,
  Pellet,
  PowerOn,
  StatusRaw,          // raw main status word, always observed (alarms visible)
  Alarm,              // experimental (F09)
  Fan2,
  Fan3,
  PelletRemaining,
  ControllerVersion,
  Count
};

template <typename T> struct Observed {
  T value{};
  bool known = false;
  uint32_t updatedAt = 0;

  // Returns true when the value changed (or became known).
  bool set(const T &v, uint32_t now) {
    bool changed = !known || value != v;
    value = v;
    known = true;
    updatedAt = now;
    return changed;
  }
};

struct StoveState {
  Observed<int16_t> roomTempTenths;
  Observed<int16_t> targetTempTenths;
  Observed<int16_t> thermocoupleC;
  Observed<StoveStatus> status;
  Observed<uint8_t> fan1;  // 1..5, 6 = auto
  Observed<uint8_t> power; // 1..5
  Observed<bool> automatic;
  Observed<bool> relax;
  Observed<bool> standby;
  Observed<bool> chrono;
  Observed<PelletLevel> pellet;
  Observed<bool> powerOn; // derived: stove is (being) lit
  Observed<uint16_t> statusRaw;         // (hi << 8) | lo of 0x02E4
  // experimental registers, raw values until their semantics are validated
  Observed<uint16_t> alarmRaw;          // 0x02E6
  Observed<uint8_t> fan2;               // 0x052A high byte
  Observed<uint8_t> fan3;               // 0x052A low byte
  Observed<uint16_t> pelletRemainingRaw; // 0x02EC
  Observed<uint16_t> controllerVersionRaw; // 0x0032

  bool hasValidResponse = false;
  uint32_t lastValidResponseAt = 0;

  void markValidResponse(uint32_t now) {
    hasValidResponse = true;
    lastValidResponseAt = now;
  }

  bool isFresh(uint32_t now, uint32_t maxAgeMs) const {
    return hasValidResponse && (uint32_t)(now - lastValidResponseAt) <= maxAgeMs;
  }
};

} // namespace edk
