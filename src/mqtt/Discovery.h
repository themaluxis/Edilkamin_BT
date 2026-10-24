// Home Assistant MQTT Discovery payloads (HA02/HA03).
// Existing unique_ids / discovery topics from v0.x are preserved so entities
// keep their entity_id after the upgrade. Documents of entities that the
// configured stove profile does not expose, and legacy v0.x documents, are
// actively removed (empty retained payload).
#pragma once
#include "mqtt/MqttManager.h"
#include "mqtt/Topics.h"
#include "stove/StoveProfile.h"

namespace edk {

struct DiscoveryContext {
  const Topics *topics = nullptr;
  const StoveProfile *profile = nullptr;
  const char *fwVersion = "dev";
  const char *stoveMac = "";   // "" when unknown
  const char *espMac = "";     // ESP32 station MAC, "" to omit
  const char *configUrl = "";  // http://<ip>/ when the web page is enabled
  const char *hwVersion = "";  // board name
  bool experimental = false;   // exp_registers: raw diagnostic sensors
  bool extThermostat = true;   // publish the external thermostat entities
};

// Publishes every discovery document (retained). Returns the number sent.
int publishDiscovery(MqttManager &mqtt, const DiscoveryContext &ctx);

// Removes every discovery document and retained state topic of an identity
// (used after topic_prefix / device_id changed). Returns the number sent.
int removeDiscovery(MqttManager &mqtt, const char *prefix, const char *deviceId);

} // namespace edk
