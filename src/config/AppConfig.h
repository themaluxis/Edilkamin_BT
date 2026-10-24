// Persistent configuration (schema v3) with validation. Pure struct; the
// LittleFS/JSON persistence lives in ConfigStore (platform layer).
#pragma once
#include <cstddef>
#include <cstdint>

namespace edk {

struct AppConfig {
  static constexpr uint8_t kSchemaVersion = 3;
  uint8_t schemaVersion = kSchemaVersion;

  // network
  char mqttServer[64] = "";
  uint16_t mqttPort = 1883;
  char mqttUser[48] = "";
  char mqttPass[64] = "";
  char ntpServer[64] = "pool.ntp.org";
  char timezone[64] = "";       // POSIX TZ, e.g. "CET-1CEST,M3.5.0,M10.5.0/3"
  long ntpOffsetSec = 7200;     // legacy fixed offset, used when timezone empty

  // identity / topics
  char topicPrefix[16] = "edilkamin";
  char deviceId[16] = "322707E4";

  // bluetooth
  char stoveMac[18] = "";       // "" = first stove advertising the service
  bool bleEnabled = true;
  uint16_t bleSessionS = 30;
  uint16_t blePauseS = 120;     // 0 = stay connected
  uint16_t blePollIntervalMs = 2000;
  uint16_t bleResponseTimeoutMs = 2000;
  uint16_t bleReconnectInitialS = 5;
  uint16_t bleReconnectMaxS = 120;
  uint16_t bleScanTimeoutS = 10;
  uint16_t bleRebootAfterMin = 0; // 0 = never reboot because of BLE

  // data / commands
  uint16_t dataMaxAgeS = 360;
  uint16_t commandExpiryS = 120;

  // diagnostics
  uint8_t logLevel = 3;      // INFO
  uint8_t mqttLogLevel = 2;  // WARN
  bool telnetEnabled = false;
  uint16_t diagIntervalS = 60;
  uint16_t wifiRebootAfterMin = 15; // 0 = never

  // local web administration (F05)
  bool webEnabled = true;
  char adminUser[32] = "admin";
  // MD5(user:realm:password) in lowercase hex; empty = no password set yet
  // (administration locked until one is chosen).
  char adminHash[33] = "";
};

// Normalises the configuration into safe ranges. Returns the number of
// fields that had to be corrected and appends short notes (if notes != null).
int validateConfig(AppConfig &cfg, char *notes = nullptr, size_t notesLen = 0);

bool isValidMac(const char *mac);          // "AA:BB:CC:DD:EE:FF"
bool isHex32(const char *s);               // 32 hex digits (MD5)
bool normalizeMac(char *mac, size_t len);  // uppercase, validates

} // namespace edk
