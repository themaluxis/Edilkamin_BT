#include "ConfigStore.h"
#include "ConfigJson.h"
#include "core/Log.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

namespace edk {

static const char *kTag = "CFG";

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
  ConfigApplyResult r = configFromJson(doc.as<JsonObjectConst>(), cfg, true);
  migrated = r.schema < AppConfig::kSchemaVersion;
  cfg.schemaVersion = AppConfig::kSchemaVersion;
  EDK_LOGI(kTag, "config loaded (schema %u, %d keys%s)", r.schema, r.applied,
           migrated ? ", migrated" : "");
  return true;
}

bool ConfigStore::save(const AppConfig &cfg) {
  JsonDocument doc;
  configToJson(cfg, doc.to<JsonObject>(), ConfigSecrets::Include);

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
