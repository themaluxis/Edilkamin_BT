#include "ConfigJson.h"
#include "core/Strings.h"
#include <cstdlib>
#include <cstring>

namespace edk {

namespace {

enum class Kind : uint8_t { Str, U8, U16, Long, Bool };

struct Field {
  const char *key;
  Kind kind;
  size_t offset;
  size_t size; // for strings
  bool secret;
};

#define EDK_FIELD(key, kind, member, secret)                                   \
  { key, Kind::kind, offsetof(AppConfig, member), sizeof(AppConfig::member),   \
    secret }

const Field kFields[] = {
    EDK_FIELD("mqtt_server", Str, mqttServer, false),
    EDK_FIELD("mqtt_port", U16, mqttPort, false),
    EDK_FIELD("mqtt_user", Str, mqttUser, false),
    EDK_FIELD("mqtt_pass", Str, mqttPass, true),
    EDK_FIELD("ntp_server", Str, ntpServer, false),
    EDK_FIELD("ntp_offset", Long, ntpOffsetSec, false),
    EDK_FIELD("tz", Str, timezone, false),
    EDK_FIELD("topic_prefix", Str, topicPrefix, false),
    EDK_FIELD("device_id", Str, deviceId, false),
    EDK_FIELD("hostname", Str, hostname, false),
    EDK_FIELD("ha_published_prefix", Str, haPublishedPrefix, false),
    EDK_FIELD("ha_published_id", Str, haPublishedId, false),
    EDK_FIELD("feat_relax", Bool, featRelax, false),
    EDK_FIELD("feat_standby", Bool, featStandby, false),
    EDK_FIELD("feat_chrono", Bool, featChrono, false),
    EDK_FIELD("stove_mac", Str, stoveMac, false),
    EDK_FIELD("ble_enabled", Bool, bleEnabled, false),
    EDK_FIELD("ble_session_s", U16, bleSessionS, false),
    EDK_FIELD("ble_pause_s", U16, blePauseS, false),
    EDK_FIELD("ble_poll_ms", U16, blePollIntervalMs, false),
    EDK_FIELD("ble_response_timeout_ms", U16, bleResponseTimeoutMs, false),
    EDK_FIELD("ble_reconnect_initial_s", U16, bleReconnectInitialS, false),
    EDK_FIELD("ble_reconnect_max_s", U16, bleReconnectMaxS, false),
    EDK_FIELD("ble_scan_timeout_s", U16, bleScanTimeoutS, false),
    EDK_FIELD("ble_reboot_after_min", U16, bleRebootAfterMin, false),
    EDK_FIELD("data_max_age_s", U16, dataMaxAgeS, false),
    EDK_FIELD("command_expiry_s", U16, commandExpiryS, false),
    EDK_FIELD("log_level", U8, logLevel, false),
    EDK_FIELD("mqtt_log_level", U8, mqttLogLevel, false),
    EDK_FIELD("telnet_enabled", Bool, telnetEnabled, false),
    EDK_FIELD("diag_interval_s", U16, diagIntervalS, false),
    EDK_FIELD("wifi_reboot_after_min", U16, wifiRebootAfterMin, false),
    EDK_FIELD("web_enabled", Bool, webEnabled, false),
    EDK_FIELD("admin_user", Str, adminUser, false),
    EDK_FIELD("admin_hash", Str, adminHash, true),
};
#undef EDK_FIELD

template <typename T> T *at(AppConfig &cfg, const Field &f) {
  return reinterpret_cast<T *>(reinterpret_cast<char *>(&cfg) + f.offset);
}
template <typename T> const T *at(const AppConfig &cfg, const Field &f) {
  return reinterpret_cast<const T *>(reinterpret_cast<const char *>(&cfg) +
                                     f.offset);
}

long numberOf(JsonVariantConst v, bool &ok) {
  ok = true;
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    if (!s || !*s) {
      ok = false;
      return 0;
    }
    char *end = nullptr;
    long n = strtol(s, &end, 10);
    ok = end && *end == '\0';
    return n;
  }
  if (v.is<bool>())
    return v.as<bool>() ? 1 : 0;
  if (v.is<long>() || v.is<float>())
    return (long)v.as<long>();
  ok = false;
  return 0;
}

bool applyField(const Field &f, JsonVariantConst v, AppConfig &cfg) {
  switch (f.kind) {
  case Kind::Str: {
    if (!v.is<const char *>())
      return false;
    const char *s = v.as<const char *>();
    copyStr(at<char>(cfg, f), f.size, s ? s : "");
    return true;
  }
  case Kind::Bool: {
    if (v.is<bool>()) {
      *at<bool>(cfg, f) = v.as<bool>();
      return true;
    }
    if (v.is<const char *>()) {
      const char *s = v.as<const char *>();
      if (!s)
        return false;
      if (strcmp(s, "true") == 0 || strcmp(s, "1") == 0 || strcmp(s, "on") == 0) {
        *at<bool>(cfg, f) = true;
        return true;
      }
      if (strcmp(s, "false") == 0 || strcmp(s, "0") == 0 || strcmp(s, "off") == 0) {
        *at<bool>(cfg, f) = false;
        return true;
      }
      return false;
    }
    bool ok;
    long n = numberOf(v, ok);
    if (ok)
      *at<bool>(cfg, f) = n != 0;
    return ok;
  }
  case Kind::U8:
  case Kind::U16:
  case Kind::Long: {
    bool ok;
    long n = numberOf(v, ok);
    if (!ok)
      return false;
    if (f.kind == Kind::U8)
      *at<uint8_t>(cfg, f) = (uint8_t)(n < 0 ? 0 : n > 255 ? 255 : n);
    else if (f.kind == Kind::U16)
      *at<uint16_t>(cfg, f) = (uint16_t)(n < 0 ? 0 : n > 65535 ? 65535 : n);
    else
      *at<long>(cfg, f) = n;
    return true;
  }
  }
  return false;
}

} // namespace

bool configKeyIsSecret(const char *key) {
  for (const Field &f : kFields)
    if (strcmp(f.key, key) == 0)
      return f.secret;
  return false;
}

void configToJson(const AppConfig &cfg, JsonObject out, ConfigSecrets secrets) {
  out["schema"] = AppConfig::kSchemaVersion;
  for (const Field &f : kFields) {
    if (f.secret && secrets == ConfigSecrets::Omit)
      continue;
    switch (f.kind) {
    case Kind::Str:
      out[f.key] = (const char *)at<char>(cfg, f);
      break;
    case Kind::Bool:
      out[f.key] = *at<bool>(cfg, f);
      break;
    case Kind::U8:
      out[f.key] = *at<uint8_t>(cfg, f);
      break;
    case Kind::U16:
      out[f.key] = *at<uint16_t>(cfg, f);
      break;
    case Kind::Long:
      out[f.key] = *at<long>(cfg, f);
      break;
    }
  }
}

ConfigApplyResult configFromJson(JsonObjectConst in, AppConfig &cfg,
                                 bool acceptSecrets) {
  ConfigApplyResult r;
  bool ok;
  long schema = numberOf(in["schema"], ok);
  r.schema = ok && schema > 0 && schema < 256 ? (uint8_t)schema : 1;
  for (JsonPairConst kv : in) {
    const char *key = kv.key().c_str();
    if (strcmp(key, "schema") == 0)
      continue;
    const Field *field = nullptr;
    for (const Field &f : kFields)
      if (strcmp(f.key, key) == 0) {
        field = &f;
        break;
      }
    if (!field) {
      r.unknown++;
      continue;
    }
    if (field->secret && !acceptSecrets) {
      r.ignored++;
      continue;
    }
    if (applyField(*field, kv.value(), cfg))
      r.applied++;
  }
  return r;
}

} // namespace edk
