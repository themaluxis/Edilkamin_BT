#include "StoveProfiles.h"
#include <cstring>

namespace edk {

static const char *const kNames[] = {"generic", "custom", nullptr};

bool isKnownProfile(const char *name) {
  if (!name)
    return false;
  for (const char *const *p = kNames; *p; p++)
    if (strcmp(*p, name) == 0)
      return true;
  return false;
}

const char *const *knownProfileNames() { return kNames; }

StoveProfile buildProfile(const AppConfig &cfg) {
  StoveProfile p = defaultProfile();
  if (strcmp(cfg.stoveProfile, "custom") == 0) {
    p.name = "custom";
    p.minTargetTenths = cfg.profMinTempTenths;
    p.maxTargetTenths = cfg.profMaxTempTenths;
    p.powerLevels = cfg.profPowerLevels;
    p.fanLevels = cfg.profFanLevels;
    p.fanAutoSupported = cfg.profFanAuto;
    p.hasFan2 = cfg.profFanCount >= 2;
    p.hasFan3 = cfg.profFanCount >= 3;
  }
  p.hasRelax = cfg.featRelax;
  p.hasStandby = cfg.featStandby;
  p.hasChrono = cfg.featChrono;
  return p;
}

} // namespace edk
