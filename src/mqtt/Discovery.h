// Home Assistant MQTT Discovery payloads (HA02 subset shipped with P0).
// Existing unique_ids / discovery topics from v0.x are preserved so entities
// keep their entity_id after the upgrade.
#pragma once
#include "mqtt/MqttManager.h"
#include "mqtt/Topics.h"
#include "stove/StoveProfile.h"

namespace edk {

struct DiscoveryContext {
  const Topics *topics = nullptr;
  const StoveProfile *profile = nullptr;
  const char *fwVersion = "dev";
  const char *stoveMac = ""; // "" when unknown
};

// Publishes every discovery document (retained). Returns the number sent.
int publishDiscovery(MqttManager &mqtt, const DiscoveryContext &ctx);

} // namespace edk
