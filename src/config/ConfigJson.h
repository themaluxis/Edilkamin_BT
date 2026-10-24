// JSON (de)serialisation of AppConfig, shared by the LittleFS store and the
// web administration (export / import / edit). Pure C++ (ArduinoJson only).
#pragma once
#include "AppConfig.h"
#include <ArduinoJson.h>

namespace edk {

enum class ConfigSecrets { Include, Omit };

// Keys whose values must never leave the device (mqtt_pass, admin_hash).
bool configKeyIsSecret(const char *key);

// Serialises cfg as flat keys (schema included). Secrets are omitted on
// request so the document can be exported or shown in a browser.
void configToJson(const AppConfig &cfg, JsonObject out, ConfigSecrets secrets);

struct ConfigApplyResult {
  int applied = 0;   // recognised keys that were applied
  int ignored = 0;   // secret keys skipped because acceptSecrets == false
  int unknown = 0;   // keys that are not part of the schema
  uint8_t schema = 1; // schema version declared by the document
};

// Applies the keys present in `in` onto cfg. Missing keys keep their value;
// legacy documents (numbers stored as strings) are accepted. Callers must run
// validateConfig() afterwards.
ConfigApplyResult configFromJson(JsonObjectConst in, AppConfig &cfg,
                                 bool acceptSecrets);

} // namespace edk
