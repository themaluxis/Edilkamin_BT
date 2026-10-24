// Named capability profiles (F08). "generic" is the validated Slide2-like
// profile; "custom" takes every value from the configuration so a user can
// describe another model without a firmware change. Pure.
#pragma once
#include "config/AppConfig.h"
#include "stove/StoveProfile.h"

namespace edk {

// Returns true when `name` is a known profile name.
bool isKnownProfile(const char *name);
// Lists known names (nullptr terminated).
const char *const *knownProfileNames();
// Builds the effective profile: named preset, custom values, feature flags.
StoveProfile buildProfile(const AppConfig &cfg);

} // namespace edk
