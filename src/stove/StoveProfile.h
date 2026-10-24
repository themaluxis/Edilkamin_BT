// Capability profile of the connected stove (F08 groundwork).
// A single generic profile is shipped for now; values match the ranges the
// original firmware exposed to Home Assistant.
#pragma once
#include <cstdint>

namespace edk {

struct StoveProfile {
  const char *name = "generic";
  int16_t minTargetTenths = 150; // 15.0 °C
  int16_t maxTargetTenths = 300; // 30.0 °C
  int16_t targetStepTenths = 5;  // 0.5 °C
  uint8_t powerLevels = 5;       // P1..P5
  uint8_t fanLevels = 5;         // 20%..100% (+ Auto = 6)
  bool fanAutoSupported = true;
  bool hasFan2 = false;
  bool hasFan3 = false;
  bool hasAirKare = false;
  bool hasChrono = true;
  bool hasRelax = true;
  bool hasStandby = true;
};

inline const StoveProfile &defaultProfile() {
  static const StoveProfile p{};
  return p;
}

} // namespace edk
