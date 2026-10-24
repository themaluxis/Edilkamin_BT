#include "StoveState.h"

namespace edk {

const char *stoveStatusLabel(StoveStatus s) {
  switch (s) {
  case StoveStatus::Off:
    return "Off";
  case StoveStatus::Cleaning:
    return "Cleaning";
  case StoveStatus::LoadingPellets:
    return "Loading Pellets";
  case StoveStatus::IgnitionChargeSuspend:
    return "Ignition - charge suspend";
  case StoveStatus::CheckExhaustTemp:
    return "Check exhaust gas temperature";
  case StoveStatus::ThresholdTempCheck:
    return "Threshold temperature check";
  case StoveStatus::Warmup:
    return "Warmup";
  case StoveStatus::Ok:
    return "Ok";
  case StoveStatus::Shutdown:
    return "Shutdown";
  case StoveStatus::Extinction:
    return "Extinction";
  case StoveStatus::Unknown:
  default:
    return "Unknown";
  }
}

} // namespace edk
