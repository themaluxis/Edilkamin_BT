// LittleFS persistence for AppConfig (JSON via ConfigJson), with migration from the v0.x
// layout (schema 1: only MQTT/NTP keys, numbers stored as strings).
#pragma once
#include "AppConfig.h"

namespace edk {

class ConfigStore {
public:
  static constexpr const char *kPath = "/config.json";

  // Returns true when a configuration file was read. `migrated` is set when
  // the file used an older schema (caller should save it back).
  bool load(AppConfig &cfg, bool &migrated);
  bool save(const AppConfig &cfg);
  bool remove();
};

} // namespace edk
