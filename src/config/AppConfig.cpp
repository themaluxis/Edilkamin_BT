#include "AppConfig.h"
#include "core/Strings.h"
#include <cstdio>
#include <cstring>

namespace edk {

bool isValidMac(const char *mac) {
  if (!mac || strlen(mac) != 17)
    return false;
  for (int i = 0; i < 17; i++) {
    char c = mac[i];
    if (i % 3 == 2) {
      if (c != ':')
        return false;
    } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                 (c >= 'A' && c <= 'F'))) {
      return false;
    }
  }
  return true;
}

bool normalizeMac(char *mac, size_t len) {
  if (!mac || len == 0)
    return false;
  trimInPlace(mac);
  // accept "aabbccddeeff" and "aa-bb-..." too
  char tmp[18];
  size_t n = strlen(mac);
  if (n == 12 && len >= 18) {
    for (int i = 0, o = 0; i < 12; i += 2) {
      tmp[o++] = mac[i];
      tmp[o++] = mac[i + 1];
      if (i < 10)
        tmp[o++] = ':';
    }
    tmp[17] = '\0';
    copyStr(mac, len, tmp);
  } else if (n == 17) {
    for (int i = 2; i < 17; i += 3)
      if (mac[i] == '-')
        mac[i] = ':';
  }
  for (size_t i = 0; i < strlen(mac); i++)
    if (mac[i] >= 'a' && mac[i] <= 'f')
      mac[i] = (char)(mac[i] - 'a' + 'A');
  return isValidMac(mac);
}

bool isHex32(const char *s) {
  if (!s || strlen(s) != 32)
    return false;
  for (int i = 0; i < 32; i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F')))
      return false;
  }
  return true;
}

template <typename T>
static bool clampField(T &v, T lo, T hi, const char *name, char *notes,
                       size_t notesLen) {
  if (v >= lo && v <= hi)
    return false;
  T old = v;
  v = v < lo ? lo : hi;
  if (notes && notesLen) {
    size_t used = strlen(notes);
    snprintf(notes + used, notesLen > used ? notesLen - used : 0,
             "%s %ld->%ld; ", name, (long)old, (long)v);
  }
  return true;
}

int validateConfig(AppConfig &cfg, char *notes, size_t notesLen) {
  int fixes = 0;
  if (notes && notesLen)
    notes[0] = '\0';
  if (cfg.mqttPort == 0) {
    cfg.mqttPort = 1883;
    fixes++;
  }
  if (cfg.ntpServer[0] == '\0') {
    copyStr(cfg.ntpServer, sizeof(cfg.ntpServer), "pool.ntp.org");
    fixes++;
  }
  if (cfg.topicPrefix[0] == '\0') {
    copyStr(cfg.topicPrefix, sizeof(cfg.topicPrefix), "edilkamin");
    fixes++;
  }
  if (cfg.deviceId[0] == '\0') {
    copyStr(cfg.deviceId, sizeof(cfg.deviceId), "322707E4");
    fixes++;
  }
  for (char *p = cfg.topicPrefix; *p; p++)
    if (*p == '/' || *p == '#' || *p == '+' || *p == ' ') {
      *p = '_';
      fixes++;
    }
  for (char *p = cfg.deviceId; *p; p++)
    if (*p == '/' || *p == '#' || *p == '+' || *p == ' ') {
      *p = '_';
      fixes++;
    }
  if (cfg.stoveMac[0] != '\0' && !normalizeMac(cfg.stoveMac, sizeof(cfg.stoveMac))) {
    cfg.stoveMac[0] = '\0';
    fixes++;
    if (notes && notesLen) {
      size_t used = strlen(notes);
      snprintf(notes + used, notesLen > used ? notesLen - used : 0,
               "stove_mac invalid, cleared; ");
    }
  }
  fixes += clampField<uint16_t>(cfg.bleSessionS, 10, 3600, "ble_session_s", notes, notesLen);
  if (cfg.blePauseS != 0)
    fixes += clampField<uint16_t>(cfg.blePauseS, 10, 3600, "ble_pause_s", notes, notesLen);
  fixes += clampField<uint16_t>(cfg.blePollIntervalMs, 500, 60000, "ble_poll_ms", notes, notesLen);
  fixes += clampField<uint16_t>(cfg.bleResponseTimeoutMs, 500, 10000, "ble_resp_timeout_ms", notes, notesLen);
  fixes += clampField<uint16_t>(cfg.bleReconnectInitialS, 1, 600, "ble_reconnect_initial_s", notes, notesLen);
  fixes += clampField<uint16_t>(cfg.bleReconnectMaxS, cfg.bleReconnectInitialS, 3600, "ble_reconnect_max_s", notes, notesLen);
  fixes += clampField<uint16_t>(cfg.bleScanTimeoutS, 3, 60, "ble_scan_timeout_s", notes, notesLen);
  if (cfg.bleRebootAfterMin != 0)
    fixes += clampField<uint16_t>(cfg.bleRebootAfterMin, 10, 1440, "ble_reboot_after_min", notes, notesLen);
  // Data must stay valid across at least one full BLE cycle.
  uint16_t cycle = (uint16_t)(cfg.bleSessionS + cfg.blePauseS);
  uint16_t minAge = (uint16_t)(cycle + 60 > 65535 ? 65535 : cycle + 60);
  fixes += clampField<uint16_t>(cfg.dataMaxAgeS, minAge, 7200, "data_max_age_s", notes, notesLen);
  fixes += clampField<uint16_t>(cfg.commandExpiryS, 10, 3600, "command_expiry_s", notes, notesLen);
  fixes += clampField<uint8_t>(cfg.logLevel, 0, 5, "log_level", notes, notesLen);
  fixes += clampField<uint8_t>(cfg.mqttLogLevel, 0, 5, "mqtt_log_level", notes, notesLen);
  fixes += clampField<uint16_t>(cfg.diagIntervalS, 10, 3600, "diag_interval_s", notes, notesLen);
  if (cfg.wifiRebootAfterMin != 0)
    fixes += clampField<uint16_t>(cfg.wifiRebootAfterMin, 5, 1440, "wifi_reboot_after_min", notes, notesLen);
  trimInPlace(cfg.adminUser);
  if (cfg.adminUser[0] == '\0') {
    copyStr(cfg.adminUser, sizeof(cfg.adminUser), "admin");
    fixes++;
  }
  for (char *p = cfg.adminUser; *p; p++)
    if (*p == ':' || *p == '"' || *p == '\\' || (unsigned char)*p < 0x20) {
      *p = '_';
      fixes++;
    }
  if (cfg.adminHash[0] != '\0' && !isHex32(cfg.adminHash)) {
    cfg.adminHash[0] = '\0';
    fixes++;
    if (notes && notesLen) {
      size_t used = strlen(notes);
      snprintf(notes + used, notesLen > used ? notesLen - used : 0,
               "admin_hash invalid, cleared; ");
    }
  }
  return fixes;
}

} // namespace edk
