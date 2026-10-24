#include "Discovery.h"
#include "core/Log.h"
#include <ArduinoJson.h>
#include <cstdio>

namespace edk {

static const char *kTag = "DISC";
static char g_payload[1800];
static char g_topic[96];
static char g_tmp[8][96];

namespace {

struct Builder {
  const DiscoveryContext &ctx;
  MqttManager &mqtt;
  int published = 0;

  const char *t(int i, const char *suffix) {
    return ctx.topics->make(g_tmp[i], sizeof(g_tmp[i]), suffix);
  }

  void device(JsonObject dev, bool full) {
    dev["identifiers"].to<JsonArray>().add(ctx.topics->deviceId());
    if (!full)
      return;
    dev["name"] = "Edilkamin";
    dev["manufacturer"] = "Edilkamin";
    dev["model"] = "The Mind pellet stove";
    dev["sw_version"] = ctx.fwVersion;
    if (ctx.stoveMac && ctx.stoveMac[0]) {
      JsonArray c = dev["connections"].to<JsonArray>().add<JsonArray>();
      c.add("bluetooth");
      c.add(ctx.stoveMac);
    }
  }

  // availability: ESP online (+ stove data fresh for stove entities)
  void availability(JsonDocument &doc, bool needsStoveData) {
    JsonArray av = doc["availability"].to<JsonArray>();
    JsonObject a = av.add<JsonObject>();
    a["topic"] = t(0, "availability/state");
    a["payload_available"] = "ONLINE";
    a["payload_not_available"] = "OFFLINE";
    if (needsStoveData) {
      JsonObject b = av.add<JsonObject>();
      b["topic"] = t(1, "data_valid/state");
      b["payload_available"] = "ONLINE";
      b["payload_not_available"] = "OFFLINE";
      doc["availability_mode"] = "all";
    }
  }

  void common(JsonDocument &doc, const char *name, const char *uidSuffix,
              bool stoveEntity, bool fullDevice, const char *category) {
    doc["name"] = name;
    char uid[64];
    snprintf(uid, sizeof(uid), "%s_edilkamin_%s", ctx.topics->deviceId(),
             uidSuffix);
    doc["unique_id"] = uid;
    availability(doc, stoveEntity);
    device(doc["device"].to<JsonObject>(), fullDevice);
    if (category)
      doc["entity_category"] = category;
  }

  void send(JsonDocument &doc, const char *component, const char *objectSuffix) {
    if (objectSuffix && *objectSuffix)
      snprintf(g_topic, sizeof(g_topic), "homeassistant/%s/edilkamin_%s_%s/config",
               component, ctx.topics->deviceId(), objectSuffix);
    else
      snprintf(g_topic, sizeof(g_topic), "homeassistant/%s/edilkamin_%s/config",
               component, ctx.topics->deviceId());
    size_t n = serializeJson(doc, g_payload, sizeof(g_payload));
    if (n == 0 || n >= sizeof(g_payload) - 1) {
      EDK_LOGE(kTag, "discovery payload too large for %s", g_topic);
      return;
    }
    if (mqtt.publish(g_topic, g_payload, true, true))
      published++;
    else
      EDK_LOGW(kTag, "discovery publish failed: %s", g_topic);
  }

  void climate() {
    JsonDocument doc;
    common(doc, "Edilkamin", "climate", true, true, nullptr);
    doc["icon"] = "mdi:gas-burner";
    doc["mode_command_topic"] = t(2, "hvac_mode/set");
    doc["mode_state_topic"] = t(3, "hvac_mode/state");
    JsonArray modes = doc["modes"].to<JsonArray>();
    modes.add("heat");
    modes.add("off");
    doc["action_topic"] = t(4, "hvac_action/state");
    doc["fan_mode_command_topic"] = t(5, "fan_mode/set");
    doc["fan_mode_state_topic"] = t(6, "fan_mode/state");
    JsonArray fans = doc["fan_modes"].to<JsonArray>();
    if (ctx.profile->fanAutoSupported)
      fans.add("Auto");
    static const char *const fanLabels[] = {"20%", "40%", "60%", "80%", "100%"};
    for (uint8_t i = 0; i < ctx.profile->fanLevels && i < 5; i++)
      fans.add(fanLabels[i]);
    doc["preset_mode_command_topic"] = t(7, "preset_mode/set");
    char presetState[96], tempCmd[96], tempState[96], curTemp[96];
    doc["preset_mode_state_topic"] =
        ctx.topics->make(presetState, sizeof(presetState), "preset_mode/state");
    JsonArray presets = doc["preset_modes"].to<JsonArray>();
    presets.add("Auto");
    static const char *const presetLabels[] = {"Man. P1", "Man. P2", "Man. P3",
                                               "Man. P4", "Man. P5"};
    for (uint8_t i = 0; i < ctx.profile->powerLevels && i < 5; i++)
      presets.add(presetLabels[i]);
    doc["temperature_command_topic"] =
        ctx.topics->make(tempCmd, sizeof(tempCmd), "target_temperature/set");
    doc["temperature_state_topic"] = ctx.topics->make(
        tempState, sizeof(tempState), "target_temperature/state");
    doc["current_temperature_topic"] =
        ctx.topics->make(curTemp, sizeof(curTemp), "temperature/state");
    doc["min_temp"] = ctx.profile->minTargetTenths / 10.0;
    doc["max_temp"] = ctx.profile->maxTargetTenths / 10.0;
    doc["temp_step"] = ctx.profile->targetStepTenths / 10.0;
    doc["precision"] = 0.1;
    doc["temperature_unit"] = "C";
    send(doc, "climate", "");
  }

  void stoveSwitch(const char *name, const char *entity, const char *uid,
                   const char *icon) {
    JsonDocument doc;
    common(doc, name, uid, true, false, nullptr);
    doc["icon"] = icon;
    char st[96];
    snprintf(st, sizeof(st), "%s/state", entity);
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "%s/set", entity);
    doc["command_topic"] = t(2, cmd);
    doc["state_topic"] = t(3, st);
    doc["payload_on"] = "ON";
    doc["payload_off"] = "OFF";
    send(doc, "switch", entity);
  }

  void stoveSensor(const char *name, const char *entity, const char *uid,
                   const char *objectSuffix, const char *icon,
                   const char *deviceClass, const char *unit,
                   const char *stateClass) {
    JsonDocument doc;
    common(doc, name, uid, true, false, nullptr);
    if (icon)
      doc["icon"] = icon;
    if (deviceClass)
      doc["device_class"] = deviceClass;
    if (unit)
      doc["unit_of_measurement"] = unit;
    if (stateClass)
      doc["state_class"] = stateClass;
    char st[96];
    snprintf(st, sizeof(st), "%s/state", entity);
    doc["state_topic"] = t(2, st);
    send(doc, "sensor", objectSuffix);
  }

  void diagSensor(const char *name, const char *uid, const char *icon,
                  const char *valueKey, const char *deviceClass,
                  const char *unit, const char *stateClass) {
    JsonDocument doc;
    common(doc, name, uid, false, false, "diagnostic");
    if (icon)
      doc["icon"] = icon;
    if (deviceClass)
      doc["device_class"] = deviceClass;
    if (unit)
      doc["unit_of_measurement"] = unit;
    if (stateClass)
      doc["state_class"] = stateClass;
    doc["state_topic"] = t(2, "diagnostics/state");
    char tpl[64];
    snprintf(tpl, sizeof(tpl), "{{ value_json.%s }}", valueKey);
    doc["value_template"] = tpl;
    send(doc, "sensor", uid);
  }

  void diagTopicSensor(const char *name, const char *uid, const char *icon,
                       const char *stateSuffix, const char *deviceClass) {
    JsonDocument doc;
    common(doc, name, uid, false, false, "diagnostic");
    if (icon)
      doc["icon"] = icon;
    if (deviceClass)
      doc["device_class"] = deviceClass;
    doc["state_topic"] = t(2, stateSuffix);
    send(doc, "sensor", uid);
  }

  void binary(const char *name, const char *uid, const char *stateSuffix,
              const char *deviceClass, const char *on, const char *off) {
    JsonDocument doc;
    common(doc, name, uid, false, false, "diagnostic");
    doc["device_class"] = deviceClass;
    doc["state_topic"] = t(2, stateSuffix);
    doc["payload_on"] = on;
    doc["payload_off"] = off;
    send(doc, "binary_sensor", uid);
  }

  void button(const char *name, const char *uid, const char *icon,
              const char *cmdSuffix, const char *payload,
              const char *deviceClass, const char *category) {
    JsonDocument doc;
    common(doc, name, uid, false, false, category);
    if (icon)
      doc["icon"] = icon;
    if (deviceClass)
      doc["device_class"] = deviceClass;
    doc["command_topic"] = t(2, cmdSuffix);
    doc["payload_press"] = payload;
    send(doc, "button", uid);
  }

  void all() {
    climate();
    // v0.x entities (same object ids / unique ids)
    stoveSwitch("Automatic Mode", "automatic_mode", "automatic_mode", "mdi:refresh-auto");
    if (ctx.profile->hasRelax)
      stoveSwitch("Relax", "relax", "relax", "mdi:volume-off");
    if (ctx.profile->hasChrono)
      stoveSwitch("Chrono", "chrono_mode", "chrono_mode", "mdi:clock-outline");
    if (ctx.profile->hasStandby)
      stoveSwitch("Standby", "standby", "standby", "mdi:sleep");
    stoveSensor("Status", "status", "status", "status", "mdi:state-machine", nullptr, nullptr, nullptr);
    stoveSensor("Exhaust Temperature", "thermocouple_temperature", "thermocouple_temperature",
                "thermocouple_temp", nullptr, "temperature", "°C", "measurement");
    stoveSensor("Pellet Level", "pellet_level", "pellet_level", "pellet_level", "mdi:grain", nullptr, nullptr, nullptr);
    // new stove entities
    stoveSensor("Room Temperature", "temperature", "room_temperature", "room_temperature",
                nullptr, "temperature", "°C", "measurement");
    // bluetooth enable switch (ESP setting, works even when the stove is away)
    {
      JsonDocument doc;
      common(doc, "Bluetooth connect", "bluetooth", false, false, "config");
      doc["icon"] = "mdi:bluetooth";
      doc["command_topic"] = t(2, "bluetooth/set");
      doc["state_topic"] = t(3, "bluetooth/state");
      send(doc, "switch", "bluetooth");
    }
    // local web administration enable (ESP setting)
    {
      JsonDocument doc;
      common(doc, "Web administration", "web_admin", false, false, "config");
      doc["icon"] = "mdi:web";
      doc["command_topic"] = t(2, "web_admin/set");
      doc["state_topic"] = t(3, "web_admin/state");
      send(doc, "switch", "web_admin");
    }
    // diagnostics
    binary("Stove connected", "ble_connected", "ble/connected", "connectivity", "ON", "OFF");
    binary("Stove data fresh", "data_valid", "data_valid/state", "connectivity", "ONLINE", "OFFLINE");
    diagTopicSensor("Bluetooth state", "ble_state", "mdi:bluetooth-settings", "ble/state", nullptr);
    diagTopicSensor("Last stove response", "last_update", nullptr, "last_update/state", "timestamp");
    diagSensor("Wi-Fi signal", "wifi_rssi", nullptr, "rssi", "signal_strength", "dBm", "measurement");
    diagSensor("Last error", "last_error", "mdi:alert-circle-outline", "last_error", nullptr, nullptr, nullptr);
    diagSensor("Firmware", "firmware", "mdi:chip", "fw", nullptr, nullptr, nullptr);
    diagSensor("Uptime", "uptime", nullptr, "uptime_s", "duration", "s", nullptr);
    diagSensor("Free memory", "free_heap", nullptr, "free_heap", "data_size", "B", "measurement");
    diagSensor("Boot count", "boot_count", "mdi:restart", "boot_count", nullptr, nullptr, "total_increasing");
    diagSensor("Reset reason", "reset_reason", "mdi:restart-alert", "reset_reason", nullptr, nullptr, nullptr);
    diagSensor("Command queue", "queue_depth", "mdi:tray-full", "queue_depth", nullptr, nullptr, "measurement");
    diagSensor("BLE connect attempts", "ble_connect_attempts", "mdi:bluetooth-connect", "ble_connect_attempts", nullptr, nullptr, "total_increasing");
    diagSensor("BLE disconnects", "ble_disconnects", "mdi:bluetooth-off", "ble_disconnects", nullptr, nullptr, "total_increasing");
    diagSensor("Requests OK", "requests_ok", "mdi:check-network-outline", "requests_ok", nullptr, nullptr, "total_increasing");
    diagSensor("Requests failed", "requests_failed", "mdi:close-network-outline", "requests_failed", nullptr, nullptr, "total_increasing");
    // log level select
    {
      JsonDocument doc;
      common(doc, "Log level", "log_level", false, false, "config");
      doc["icon"] = "mdi:text-box-search-outline";
      doc["command_topic"] = t(2, "log_level/set");
      doc["state_topic"] = t(3, "log_level/state");
      JsonArray opts = doc["options"].to<JsonArray>();
      for (const char *o : {"OFF", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"})
        opts.add(o);
      send(doc, "select", "log_level");
    }
    button("Restart ESP32", "restart", nullptr, "restart/set", "RESTART", "restart", "config");
    button("Dump logs to MQTT", "log_dump", "mdi:text-long", "log_dump/set", "DUMP", nullptr, "diagnostic");
    button("Reset counters", "diagnostics_reset", "mdi:counter", "diagnostics_reset/set", "RESET", nullptr, "diagnostic");
  }
};

} // namespace

int publishDiscovery(MqttManager &mqtt, const DiscoveryContext &ctx) {
  Builder b{ctx, mqtt};
  b.all();
  EDK_LOGI(kTag, "published %d discovery documents", b.published);
  return b.published;
}

} // namespace edk
