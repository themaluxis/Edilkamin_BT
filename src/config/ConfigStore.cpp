#include "ConfigStore.h"
#include "core/Log.h"
#include "core/Strings.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

namespace edk {

static const char *kTag = "CFG";

template <size_t N>
static void readStr(JsonVariantConst v, char (&dst)[N]) {
  if (v.is<const char *>() && v.as<const char *>() != nullptr)
    copyStr(dst, N, v.as<const char *>());
}

template <typename T> static void readNum(JsonVariantConst v, T &dst) {
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    if (s && *s)
      dst = (T)atol(s);
  } else if (!v.isNull()) {
    dst = v.as<T>();
  }
}

static void readBool(JsonVariantConst v, bool &dst) {
  if (v.is<bool>())
    dst = v.as<bool>();
  else if (v.is<int>())
    dst = v.as<int>() != 0;
}

bool ConfigStore::load(AppConfig &cfg, bool &migrated) {
  migrated = false;
  if (!LittleFS.exists(kPath))
    return false;
  File f = LittleFS.open(kPath, "r");
  if (!f) {
    EDK_LOGE(kTag, "cannot open %s", kPath);
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    EDK_LOGE(kTag, "config parse error: %s", err.c_str());
    return false;
  }
  uint8_t schema = 1;
  readNum(doc["schema"], schema);
  migrated = schema < AppConfig::kSchemaVersion;

  readStr(doc["mqtt_server"], cfg.mqttServer);
  readNum(doc["mqtt_port"], cfg.mqttPort);
  readStr(doc["mqtt_user"], cfg.mqttUser);
  readStr(doc["mqtt_pass"], cfg.mqttPass);
  readStr(doc["ntp_server"], cfg.ntpServer);
  readNum(doc["ntp_offset"], cfg.ntpOffsetSec);
  readStr(doc["tz"], cfg.timezone);
  readStr(doc["topic_prefix"], cfg.topicPrefix);
  readStr(doc["device_id"], cfg.deviceId);
  readStr(doc["stove_mac"], cfg.stoveMac);
  readBool(doc["ble_enabled"], cfg.bleEnabled);
  readNum(doc["ble_session_s"], cfg.bleSessionS);
  readNum(doc["ble_pause_s"], cfg.blePauseS);
  readNum(doc["ble_poll_ms"], cfg.blePollIntervalMs);
  readNum(doc["ble_response_timeout_ms"], cfg.bleResponseTimeoutMs);
  readNum(doc["ble_reconnect_initial_s"], cfg.bleReconnectInitialS);
  readNum(doc["ble_reconnect_max_s"], cfg.bleReconnectMaxS);
  readNum(doc["ble_scan_timeout_s"], cfg.bleScanTimeoutS);
  readNum(doc["ble_reboot_after_min"], cfg.bleRebootAfterMin);
  readNum(doc["data_max_age_s"], cfg.dataMaxAgeS);
  readNum(doc["command_expiry_s"], cfg.commandExpiryS);
  readNum(doc["log_level"], cfg.logLevel);
  readNum(doc["mqtt_log_level"], cfg.mqttLogLevel);
  readBool(doc["telnet_enabled"], cfg.telnetEnabled);
  readNum(doc["diag_interval_s"], cfg.diagIntervalS);
  readNum(doc["wifi_reboot_after_min"], cfg.wifiRebootAfterMin);
  cfg.schemaVersion = AppConfig::kSchemaVersion;
  EDK_LOGI(kTag, "config loaded (schema %u%s)", schema,
           migrated ? ", migrated" : "");
  return true;
}

bool ConfigStore::save(const AppConfig &cfg) {
  JsonDocument doc;
  doc["schema"] = AppConfig::kSchemaVersion;
  doc["mqtt_server"] = cfg.mqttServer;
  doc["mqtt_port"] = cfg.mqttPort;
  doc["mqtt_user"] = cfg.mqttUser;
  doc["mqtt_pass"] = cfg.mqttPass;
  doc["ntp_server"] = cfg.ntpServer;
  doc["ntp_offset"] = cfg.ntpOffsetSec;
  doc["tz"] = cfg.timezone;
  doc["topic_prefix"] = cfg.topicPrefix;
  doc["device_id"] = cfg.deviceId;
  doc["stove_mac"] = cfg.stoveMac;
  doc["ble_enabled"] = cfg.bleEnabled;
  doc["ble_session_s"] = cfg.bleSessionS;
  doc["ble_pause_s"] = cfg.blePauseS;
  doc["ble_poll_ms"] = cfg.blePollIntervalMs;
  doc["ble_response_timeout_ms"] = cfg.bleResponseTimeoutMs;
  doc["ble_reconnect_initial_s"] = cfg.bleReconnectInitialS;
  doc["ble_reconnect_max_s"] = cfg.bleReconnectMaxS;
  doc["ble_scan_timeout_s"] = cfg.bleScanTimeoutS;
  doc["ble_reboot_after_min"] = cfg.bleRebootAfterMin;
  doc["data_max_age_s"] = cfg.dataMaxAgeS;
  doc["command_expiry_s"] = cfg.commandExpiryS;
  doc["log_level"] = cfg.logLevel;
  doc["mqtt_log_level"] = cfg.mqttLogLevel;
  doc["telnet_enabled"] = cfg.telnetEnabled;
  doc["diag_interval_s"] = cfg.diagIntervalS;
  doc["wifi_reboot_after_min"] = cfg.wifiRebootAfterMin;

  // Write to a temp file first so a power cut cannot leave a truncated config.
  const char *tmp = "/config.json.tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) {
    EDK_LOGE(kTag, "cannot write %s", tmp);
    return false;
  }
  size_t written = serializeJson(doc, f);
  f.close();
  if (written == 0) {
    EDK_LOGE(kTag, "config serialisation failed");
    LittleFS.remove(tmp);
    return false;
  }
  LittleFS.remove(kPath);
  if (!LittleFS.rename(tmp, kPath)) {
    EDK_LOGE(kTag, "rename %s failed", tmp);
    return false;
  }
  EDK_LOGI(kTag, "config saved (%u bytes)", (unsigned)written);
  return true;
}

bool ConfigStore::remove() {
  bool ok = true;
  if (LittleFS.exists(kPath))
    ok = LittleFS.remove(kPath);
  return ok;
}

} // namespace edk
