// Edilkamin_BT Enhanced - ESP32 firmware entry point.
//
// Wiring only: configuration, Wi-Fi, MQTT, BLE and the stove controller are
// implemented in their own modules (see docs/architecture.md).
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <TelnetStream.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_arduino_version.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <time.h>

#include "ble/BleManager.h"
#include "config/AppConfig.h"
#include "config/ConfigStore.h"
#include "core/Log.h"
#include "core/Strings.h"
#include "diagnostics/Diagnostics.h"
#include "logging/Logger.h"
#include "mqtt/Discovery.h"
#include "mqtt/MqttManager.h"
#include "mqtt/Topics.h"
#include "protocol/Decoders.h"
#include "stove/StoveController.h"
#include "web/WebAdmin.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

using namespace edk;

static const char *kTag = "MAIN";
static const char *kHostname = "edilkaminble";
static const uint32_t kTempLogLevelMs = 30UL * 60UL * 1000UL; // DEBUG/TRACE auto-revert

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static AppConfig cfg;
static ConfigStore store;
static Topics topics;
static WiFiClient net;
static MqttManager mqtt;
static BleManager ble;
static WebAdmin web;
static Preferences prefs;
static ErrorTracker lastError;

static bool shouldSaveConfig = false;
static bool ntpSynced = false;
static uint32_t bootCount = 0;
static char lastRestartCause[48] = "";
static uint32_t wifiDownSince = 0;
static uint32_t wifiReconnects = 0;
static uint32_t lastWifiRetryAt = 0;
static uint32_t lastDiagPublishAt = 0;
static uint32_t lastUpdatePublishAt = 0;
static time_t lastValidWall = 0;
static time_t lastErrorWall = 0;
static bool telnetActive = false;
static bool mdnsActive = false;

static char topicBuf[96];
static char payloadBuf[1024];

// ---------------------------------------------------------------------------
// Time helpers
// ---------------------------------------------------------------------------
static bool timeIsSynced() { return time(nullptr) > 1600000000; }

static bool wallClock(char *out, size_t n) {
  if (!timeIsSynced())
    return false;
  time_t t = time(nullptr);
  struct tm tmv;
  localtime_r(&t, &tmv);
  strftime(out, n, "%H:%M:%S", &tmv);
  return true;
}

static const char *isoTime(time_t t, char *buf, size_t n) {
  if (t <= 0) {
    buf[0] = '\0';
    return buf;
  }
  struct tm tmv;
  gmtime_r(&t, &tmv);
  strftime(buf, n, "%Y-%m-%dT%H:%M:%SZ", &tmv);
  return buf;
}

static uint32_t uptimeMs() { return millis(); }
static void applyLogLevel(LogLevel level);
static void mqttSink(LogLevel, const char *line, void *);
static void factoryReset(const char *origin);

static const char *resetReasonName(esp_reset_reason_t r) {
  switch (r) {
  case ESP_RST_POWERON:
    return "power on";
  case ESP_RST_EXT:
    return "external reset";
  case ESP_RST_SW:
    return "software restart";
  case ESP_RST_PANIC:
    return "panic";
  case ESP_RST_INT_WDT:
    return "interrupt watchdog";
  case ESP_RST_TASK_WDT:
    return "task watchdog";
  case ESP_RST_WDT:
    return "watchdog";
  case ESP_RST_DEEPSLEEP:
    return "deep sleep";
  case ESP_RST_BROWNOUT:
    return "brownout";
  case ESP_RST_SDIO:
    return "sdio";
  default:
    return "unknown";
  }
}

// ---------------------------------------------------------------------------
// Log sinks
// ---------------------------------------------------------------------------
static void serialSink(LogLevel, const char *line, void *) {
  Serial.println(line);
}

static void telnetSink(LogLevel, const char *line, void *) {
  if (telnetActive)
    TelnetStream.println(line);
}

static void mqttSink(LogLevel, const char *line, void *) {
  static bool inside = false; // MQTT publishing itself may log
  if (inside || !mqtt.connected())
    return;
  inside = true;
  mqtt.publish(topics.make(topicBuf, sizeof(topicBuf), "log"), line, false,
               true);
  inside = false;
}

// ---------------------------------------------------------------------------
// Restart with a persisted cause (reported after reboot)
// ---------------------------------------------------------------------------
static void restartWithCause(const char *cause) {
  EDK_LOGW(kTag, "restarting: %s", cause);
  prefs.putString("cause", cause);
  if (mqtt.connected()) {
    mqtt.publish(topics.make(topicBuf, sizeof(topicBuf), "availability/state"),
                 "OFFLINE", true, true);
  }
  Serial.flush();
  delay(250);
  ESP.restart();
}

// ---------------------------------------------------------------------------
// MQTT publishing helpers
// ---------------------------------------------------------------------------
static bool pubState(const char *entity, const char *payload,
                     bool retain = true, bool force = false) {
  return mqtt.publish(topics.state(topicBuf, sizeof(topicBuf), entity), payload,
                      retain, force);
}

static bool pubRaw(const char *suffix, const char *payload, bool retain = true,
                   bool force = false) {
  return mqtt.publish(topics.make(topicBuf, sizeof(topicBuf), suffix), payload,
                      retain, force);
}

static void publishBleState(bool force = false) {
  const BleSessionPolicy &p = ble.policy();
  pubRaw("ble/state", p.stateName(), true, force);
  pubRaw("ble/connected", p.linkUp() ? "ON" : "OFF", true, force);
  pubRaw("bluetooth/state", p.enabled() ? "ON" : "OFF", true, force);
}

static void publishLastUpdate(bool force) {
  if (lastValidWall <= 0)
    return;
  char iso[32];
  pubRaw("last_update/state", isoTime(lastValidWall, iso, sizeof(iso)), true,
         force);
  lastUpdatePublishAt = millis();
}

static StoveController *g_ctl = nullptr;

static const char *hvacActionFor(StoveStatus s) {
  switch (s) {
  case StoveStatus::Off:
    return "off";
  case StoveStatus::Ok:
    return "heating";
  case StoveStatus::Cleaning:
  case StoveStatus::LoadingPellets:
  case StoveStatus::IgnitionChargeSuspend:
  case StoveStatus::CheckExhaustTemp:
  case StoveStatus::ThresholdTempCheck:
  case StoveStatus::Warmup:
    return "preheating";
  case StoveStatus::Shutdown:
  case StoveStatus::Extinction:
    return "idle";
  default:
    return nullptr;
  }
}

static void publishObservable(const StoveState &s, Observable what,
                              bool force) {
  char num[16];
  switch (what) {
  case Observable::RoomTemp:
    if (s.roomTempTenths.known)
      pubState("temperature", formatTenths(s.roomTempTenths.value, num, sizeof(num)), true, force);
    break;
  case Observable::TargetTemp:
    if (s.targetTempTenths.known)
      pubState("target_temperature", formatTenths(s.targetTempTenths.value, num, sizeof(num)), true, force);
    break;
  case Observable::Thermocouple:
    if (s.thermocoupleC.known) {
      snprintf(num, sizeof(num), "%d", (int)s.thermocoupleC.value);
      pubState("thermocouple_temperature", num, true, force);
    }
    break;
  case Observable::Status:
    if (s.status.known) {
      pubState("status", stoveStatusLabel(s.status.value), true, force);
      const char *action = hvacActionFor(s.status.value);
      if (action)
        pubState("hvac_action", action, true, force);
    }
    break;
  case Observable::Fan1:
    if (s.fan1.known && fanModeLabel(s.fan1.value))
      pubState("fan_mode", fanModeLabel(s.fan1.value), true, force);
    break;
  case Observable::Power:
  case Observable::Automatic:
    if (s.automatic.known)
      pubState("automatic_mode", s.automatic.value ? "ON" : "OFF", true, force);
    if (s.automatic.known && s.automatic.value)
      pubState("preset_mode", "Auto", true, force);
    else if (s.power.known && presetModeLabel(s.power.value))
      pubState("preset_mode", presetModeLabel(s.power.value), true, force);
    break;
  case Observable::Relax:
    if (s.relax.known)
      pubState("relax", s.relax.value ? "ON" : "OFF", true, force);
    break;
  case Observable::Standby:
    if (s.standby.known)
      pubState("standby", s.standby.value ? "ON" : "OFF", true, force);
    break;
  case Observable::Chrono:
    if (s.chrono.known)
      pubState("chrono_mode", s.chrono.value ? "ON" : "OFF", true, force);
    break;
  case Observable::Pellet:
    if (s.pellet.known)
      pubState("pellet_level", s.pellet.value == PelletLevel::Empty ? "Empty" : "Ok", true, force);
    break;
  case Observable::PowerOn:
    if (s.powerOn.known)
      pubState("hvac_mode", s.powerOn.value ? "heat" : "off", true, force);
    break;
  default:
    break;
  }
}

static void publishAllStates(bool force) {
  if (!g_ctl)
    return;
  const StoveState &s = g_ctl->state();
  for (uint8_t i = 0; i < (uint8_t)Observable::Count; i++)
    publishObservable(s, (Observable)i, force);
  pubRaw("data_valid/state", g_ctl->dataValid() ? "ONLINE" : "OFFLINE", true, force);
  pubRaw("log_level/state", logLevelName(logger().level()), true, force);
  pubRaw("web_admin/state", cfg.webEnabled ? "ON" : "OFF", true, force);
  publishBleState(force);
  publishLastUpdate(force);
}

static void publishDiagnostics(bool force) {
  if (!g_ctl || !mqtt.connected())
    return;
  const BleSessionPolicy &p = ble.policy();
  const StoveController::Stats &st = g_ctl->stats();
  char isoErr[32], isoValid[32];
  String ip = WiFi.localIP().toString();
  DiagSnapshot d;
  d.firmware = FW_VERSION;
  d.bleState = p.stateName();
  d.bleConnected = p.linkUp();
  d.bleEnabled = p.enabled();
  d.stoveMac = ble.foundMac()[0] ? ble.foundMac() : cfg.stoveMac;
  d.bleConnectAttempts = p.stats().connectAttempts;
  d.bleSessionsOk = p.stats().sessionsOk;
  d.bleDisconnects = p.stats().linkDrops;
  d.bleScanNotFound = p.stats().scanNotFound;
  d.requestsOk = st.requestsOk;
  d.requestsFailed = st.requestsTimeout + st.framesInvalid;
  d.framesInvalid = st.framesInvalid;
  d.commandsRejected = st.commandsRejected;
  d.commandsFailed = st.commandsFailed;
  d.queueDepth = g_ctl->queue().size();
  d.queueCapacity = g_ctl->queue().capacity();
  d.queueOverflows = g_ctl->queue().overflowCount();
  d.mqttReconnects = mqtt.reconnectCount();
  d.wifiReconnects = wifiReconnects;
  d.wifiRssi = (int8_t)WiFi.RSSI();
  d.ip = ip.c_str();
  d.uptimeS = millis() / 1000UL;
  d.freeHeap = ESP.getFreeHeap();
  d.minFreeHeap = ESP.getMinFreeHeap();
  d.bootCount = bootCount;
  d.resetReason = resetReasonName(esp_reset_reason());
  d.lastRestartCause = lastRestartCause;
  d.lastError = lastError.message();
  d.lastErrorAt = isoTime(lastErrorWall, isoErr, sizeof(isoErr));
  d.lastValidAt = isoTime(lastValidWall, isoValid, sizeof(isoValid));
  d.dataValid = g_ctl->dataValid();
  d.ntpSynced = ntpSynced;
  d.logLevel = logLevelName(logger().level());
  size_t n = buildDiagnosticsJson(d, payloadBuf, sizeof(payloadBuf));
  if (n >= sizeof(payloadBuf)) {
    EDK_LOGW(kTag, "diagnostics JSON truncated");
    return;
  }
  pubRaw("diagnostics/state", payloadBuf, true, force);
  lastDiagPublishAt = millis();
}

static void recordError(const char *msg) {
  lastError.set(msg, millis());
  lastErrorWall = timeIsSynced() ? time(nullptr) : 0;
}

// ---------------------------------------------------------------------------
// Controller events → MQTT
// ---------------------------------------------------------------------------
class Events : public IStoveEvents {
public:
  void onCommandResult(const Command &c, const char *reason) override {
    EDK_LOGI(kTag, "command #%u %s '%s' -> %s %s", c.id,
             commandTypeName(c.type), c.payload, commandStateName(c.state),
             reason ? reason : "");
    char payload[32], why[160], json[320];
    jsonEscape(c.payload, payload, sizeof(payload));
    jsonEscape(reason ? reason : "", why, sizeof(why));
    snprintf(json, sizeof(json),
             "{\"id\":%u,\"type\":\"%s\",\"entity\":\"%s\",\"payload\":\"%s\","
             "\"state\":\"%s\",\"reason\":\"%s\"}",
             c.id, commandTypeName(c.type), commandEntity(c.type), payload,
             commandStateName(c.state), why);
    pubRaw("command/result", json, false, true);
  }
  void onObservedChanged(const StoveState &s, Observable what) override {
    publishObservable(s, what, false);
  }
  void onDataValidChanged(bool valid) override {
    pubRaw("data_valid/state", valid ? "ONLINE" : "OFFLINE", true, true);
    publishLastUpdate(true);
    publishDiagnostics(true);
  }
  void onValidResponse(uint32_t now) override {
    ble.policy().onValidResponse(now);
    if (timeIsSynced())
      lastValidWall = time(nullptr);
    if ((uint32_t)(now - lastUpdatePublishAt) >= 60000UL)
      publishLastUpdate(false);
  }
  void onError(const char *message) override {
    EDK_LOGW(kTag, "%s", message);
    recordError(message);
    publishDiagnostics(true);
  }
  void onUserCommandQueued() override { ble.requestSession(millis()); }
};

static Events events;
static StoveController controller(ble, events);

// ---------------------------------------------------------------------------
// BLE callbacks
// ---------------------------------------------------------------------------
static void onBleFrame(const uint8_t plain[kBleBlockSize], void *) {
  controller.onFrame(plain, millis());
}

static void onBleState(BleState state, void *) {
  uint32_t now = millis();
  bool ready = state == BleState::Connected || state == BleState::Communicating;
  controller.setLinkReady(ready, now);
  publishBleState(false);
  if (!ready)
    publishLastUpdate(false);
  if (state == BleState::Error) {
    char msg[96];
    const BleErrorReason reason = ble.policy().lastError();
    if (reason == BleErrorReason::ConnectFailed) {
      const int rc = ble.lastConnectError();
      snprintf(msg, sizeof(msg), "BLE: connect failed (rc=%d/0x%X%s)",
               rc, (unsigned)rc, rc ? "" : "; no stack code");
    } else {
      snprintf(msg, sizeof(msg), "BLE: %s", bleErrorReasonName(reason));
    }
    recordError(msg);
  }
  // Refresh the diagnostics at session boundaries and on errors only.
  if (state == BleState::Error || state == BleState::Idle ||
      state == BleState::Disabled)
    publishDiagnostics(true);
}

// ---------------------------------------------------------------------------
// Web administration hooks
// ---------------------------------------------------------------------------
static WebAdmin::Hooks webHooks();

static void startWebAdmin() {
  if (!cfg.webEnabled || web.running() || !WiFi.isConnected())
    return;
  web.begin(cfg, store, webHooks(), FW_VERSION);
  if (!mdnsActive) {
    mdnsActive = MDNS.begin(kHostname);
    if (mdnsActive)
      MDNS.addService("http", "tcp", 80);
    else
      EDK_LOGW(kTag, "mDNS start failed");
  }
}

static void setWebEnabled(bool enabled) {
  if (cfg.webEnabled != enabled) {
    cfg.webEnabled = enabled;
    store.save(cfg);
  }
  if (enabled)
    startWebAdmin();
  else
    web.stop();
  pubRaw("web_admin/state", enabled ? "ON" : "OFF", true, true);
}

static const char *runningPartitionLabel(uint32_t &size) {
  const esp_partition_t *run = esp_ota_get_running_partition();
  size = run ? run->size : 0;
  return run ? run->label : "?";
}

static size_t webStatusJson(char *buf, size_t n, void *) {
  JsonDocument doc;
  const BleSessionPolicy &p = ble.policy();
  const StoveController::Stats &st = controller.stats();
  const StoveState &s = controller.state();
  char iso[32];

  JsonObject sys = doc["system"].to<JsonObject>();
  sys["fw"] = FW_VERSION;
  sys["build"] = __DATE__ " " __TIME__;
  char core[24];
  snprintf(core, sizeof(core), "%d.%d.%d", ESP_ARDUINO_VERSION_MAJOR,
           ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
  sys["core"] = core;
  sys["sdk"] = ESP.getSdkVersion();
  char chip[48];
  snprintf(chip, sizeof(chip), "%s rev%d, %u cores, %u MHz", ESP.getChipModel(),
           ESP.getChipRevision(), ESP.getChipCores(), (unsigned)ESP.getCpuFreqMHz());
  sys["chip"] = chip;
  sys["flash_size"] = ESP.getFlashChipSize();
  sys["sketch_size"] = ESP.getSketchSize();
  sys["free_sketch"] = ESP.getFreeSketchSpace();
  uint32_t partSize = 0;
  sys["partition"] = runningPartitionLabel(partSize);
  sys["partition_size"] = partSize;
  sys["ota_capable"] = esp_ota_get_next_update_partition(nullptr) != nullptr;
  sys["uptime_s"] = millis() / 1000UL;
  sys["free_heap"] = ESP.getFreeHeap();
  sys["min_free_heap"] = ESP.getMinFreeHeap();
  sys["boot_count"] = bootCount;
  sys["reset_reason"] = resetReasonName(esp_reset_reason());
  sys["last_restart_cause"] = lastRestartCause;
  sys["ntp_synced"] = ntpSynced;
  {
    char wall[32] = "";
    if (timeIsSynced()) {
      time_t t = time(nullptr);
      struct tm tmv;
      localtime_r(&t, &tmv);
      strftime(wall, sizeof(wall), "%Y-%m-%d %H:%M:%S", &tmv);
    }
    sys["time"] = wall;
  }

  JsonObject w = doc["wifi"].to<JsonObject>();
  w["ssid"] = WiFi.SSID();
  w["ip"] = WiFi.localIP().toString();
  w["rssi"] = WiFi.RSSI();
  w["channel"] = WiFi.channel();
  w["mac"] = WiFi.macAddress();
  w["hostname"] = kHostname;

  JsonObject m = doc["mqtt"].to<JsonObject>();
  m["connected"] = mqtt.connected();
  m["server"] = cfg.mqttServer;
  m["port"] = cfg.mqttPort;

  JsonObject b = doc["ble"].to<JsonObject>();
  b["state"] = p.stateName();
  b["connected"] = p.linkUp();
  b["enabled"] = p.enabled();
  b["mac"] = ble.foundMac()[0] ? ble.foundMac() : cfg.stoveMac;
  b["last_connect_error"] = ble.lastConnectError();

  JsonObject so = doc["stove"].to<JsonObject>();
  so["data_valid"] = controller.dataValid();
  if (s.status.known) {
    so["status"] = stoveStatusLabel(s.status.value);
    so["power_on"] = s.powerOn.known && s.powerOn.value;
  }
  if (s.roomTempTenths.known)
    so["room_temp_tenths"] = s.roomTempTenths.value;
  if (s.targetTempTenths.known)
    so["target_temp_tenths"] = s.targetTempTenths.value;
  if (s.thermocoupleC.known)
    so["thermocouple_c"] = s.thermocoupleC.value;
  if (s.power.known)
    so["power"] = s.power.value;
  if (s.fan1.known)
    so["fan"] = s.fan1.value;
  if (s.automatic.known)
    so["automatic"] = s.automatic.value;
  if (s.relax.known)
    so["relax"] = s.relax.value;
  if (s.standby.known)
    so["standby"] = s.standby.value;
  if (s.chrono.known)
    so["chrono"] = s.chrono.value;
  if (s.pellet.known)
    so["pellet"] = s.pellet.value == PelletLevel::Empty ? "Empty" : "Ok";
  so["last_valid_at"] = isoTime(lastValidWall, iso, sizeof(iso));

  JsonObject c = doc["counters"].to<JsonObject>();
  c["ble_connect_attempts"] = p.stats().connectAttempts;
  c["ble_sessions_ok"] = p.stats().sessionsOk;
  c["ble_disconnects"] = p.stats().linkDrops;
  c["ble_scan_not_found"] = p.stats().scanNotFound;
  c["requests_ok"] = st.requestsOk;
  c["requests_failed"] = st.requestsTimeout + st.framesInvalid;
  c["commands_rejected"] = st.commandsRejected;
  c["commands_failed"] = st.commandsFailed;
  c["queue_depth"] = controller.queue().size();
  c["queue_capacity"] = controller.queue().capacity();
  c["queue_overflows"] = controller.queue().overflowCount();
  c["mqtt_reconnects"] = mqtt.reconnectCount();
  c["wifi_reconnects"] = wifiReconnects;
  c["web_auth_failures"] = web.authFailures();

  doc["last_error"] = lastError.message();
  char isoErr[32];
  doc["last_error_at"] = isoTime(lastErrorWall, isoErr, sizeof(isoErr));
  doc["log_level"] = logLevelName(logger().level());
  doc["log_level_temporary"] = logger().temporary();
  doc["restart_pending"] = web.restartPending();
  return serializeJson(doc, buf, n);
}

// Applies what can change live; reports whether a restart is needed.
static bool webApplyConfig(const AppConfig &next, bool &restartRequired,
                           char *msg, size_t msgLen, void *) {
  (void)msg;
  (void)msgLen;
  const AppConfig &cur = cfg;
  restartRequired =
      strcmp(next.mqttServer, cur.mqttServer) != 0 || next.mqttPort != cur.mqttPort ||
      strcmp(next.mqttUser, cur.mqttUser) != 0 || strcmp(next.mqttPass, cur.mqttPass) != 0 ||
      strcmp(next.ntpServer, cur.ntpServer) != 0 || strcmp(next.timezone, cur.timezone) != 0 ||
      next.ntpOffsetSec != cur.ntpOffsetSec ||
      strcmp(next.topicPrefix, cur.topicPrefix) != 0 || strcmp(next.deviceId, cur.deviceId) != 0 ||
      strcmp(next.stoveMac, cur.stoveMac) != 0 ||
      next.bleSessionS != cur.bleSessionS || next.blePauseS != cur.blePauseS ||
      next.blePollIntervalMs != cur.blePollIntervalMs ||
      next.bleResponseTimeoutMs != cur.bleResponseTimeoutMs ||
      next.bleReconnectInitialS != cur.bleReconnectInitialS ||
      next.bleReconnectMaxS != cur.bleReconnectMaxS ||
      next.bleScanTimeoutS != cur.bleScanTimeoutS ||
      next.telnetEnabled != cur.telnetEnabled;

  // live changes
  if (next.bleEnabled != cur.bleEnabled) {
    ble.setEnabled(next.bleEnabled, millis());
    controller.setBleEnabled(next.bleEnabled);
    pubRaw("bluetooth/state", next.bleEnabled ? "ON" : "OFF", true, true);
  }
  if (next.logLevel != cur.logLevel) {
    logger().setLevel((LogLevel)next.logLevel);
    pubRaw("log_level/state", logLevelName(logger().level()), true, true);
  }
  if (next.mqttLogLevel != cur.mqttLogLevel)
    logger().setSinkLevel(mqttSink, (LogLevel)next.mqttLogLevel);
  if (next.dataMaxAgeS != cur.dataMaxAgeS || next.commandExpiryS != cur.commandExpiryS) {
    StoveController::Config cc = controller.config();
    cc.dataMaxAgeMs = (uint32_t)next.dataMaxAgeS * 1000UL;
    cc.commandExpiryMs = (uint32_t)next.commandExpiryS * 1000UL;
    controller.configure(cc);
  }
  if (next.webEnabled != cur.webEnabled)
    pubRaw("web_admin/state", next.webEnabled ? "ON" : "OFF", true, true);
  return true;
}

static void webSetLogLevel(LogLevel level, void *) { applyLogLevel(level); }
static void webRestart(const char *cause, void *) { restartWithCause(cause); }
static void webFactoryReset(void *) { factoryReset("web request"); }

static void webSetWifi(const char *ssid, const char *pass, void *) {
  EDK_LOGW(kTag, "storing new Wi-Fi credentials for '%s' and restarting", ssid);
  prefs.putString("cause", "wifi credentials changed");
  if (mqtt.connected())
    mqtt.publish(topics.make(topicBuf, sizeof(topicBuf), "availability/state"),
                 "OFFLINE", true, true);
  WiFi.persistent(true);
  WiFi.begin(ssid, pass[0] ? pass : nullptr); // written to NVS by the driver
  delay(500);
  Serial.flush();
  ESP.restart();
}

static WebAdmin::Hooks webHooks() {
  WebAdmin::Hooks h;
  h.statusJson = webStatusJson;
  h.applyConfig = webApplyConfig;
  h.setLogLevel = webSetLogLevel;
  h.restart = webRestart;
  h.factoryReset = webFactoryReset;
  h.setWifi = webSetWifi;
  return h;
}

// ---------------------------------------------------------------------------
// MQTT callbacks
// ---------------------------------------------------------------------------
static void setBleEnabled(bool enabled) {
  if (cfg.bleEnabled != enabled) {
    cfg.bleEnabled = enabled;
    store.save(cfg);
  }
  ble.setEnabled(enabled, millis());
  controller.setBleEnabled(enabled);
  pubRaw("bluetooth/state", enabled ? "ON" : "OFF", true, true);
}

static void applyLogLevel(LogLevel level) {
  bool temporary = level >= LogLevel::Debug;
  logger().setLevel(level, temporary ? kTempLogLevelMs : 0);
  EDK_LOGI(kTag, "log level set to %s%s", logLevelName(level),
           temporary ? " (reverts after 30 min)" : "");
  pubRaw("log_level/state", logLevelName(level), true, true);
}

static void dumpLogLine(const char *line, void *) {
  mqtt.publish(topics.make(topicBuf, sizeof(topicBuf), "log"), line, false, true);
  delay(2);
}

static void onMqttMessage(const char *topic, const char *payload, void *) {
  uint32_t now = millis();
  if (strcmp(topic, "homeassistant/status") == 0) {
    if (strcmp(payload, "online") == 0) {
      EDK_LOGI(kTag, "Home Assistant came online, republishing");
      DiscoveryContext dc;
      dc.topics = &topics;
      dc.profile = &defaultProfile();
      dc.fwVersion = FW_VERSION;
      dc.stoveMac = ble.foundMac()[0] ? ble.foundMac() : cfg.stoveMac;
      publishDiscovery(mqtt, dc);
      publishAllStates(true);
      publishDiagnostics(true);
    }
    return;
  }
  char entity[32];
  if (!topics.parseSet(topic, entity, sizeof(entity))) {
    EDK_LOGD(kTag, "ignoring message on %s", topic);
    return;
  }
  if (payload[0] == '\0')
    return; // our own retained-clear or an empty command
  if (strcmp(entity, "bluetooth") == 0) {
    if (strcmp(payload, "ON") == 0)
      setBleEnabled(true);
    else if (strcmp(payload, "OFF") == 0)
      setBleEnabled(false);
    return;
  }
  if (strcmp(entity, "web_admin") == 0) {
    if (strcmp(payload, "ON") == 0)
      setWebEnabled(true);
    else if (strcmp(payload, "OFF") == 0)
      setWebEnabled(false);
    return;
  }
  if (strcmp(entity, "log_level") == 0) {
    LogLevel level;
    if (parseLogLevel(payload, level))
      applyLogLevel(level);
    else
      EDK_LOGW(kTag, "unknown log level '%s'", payload);
    return;
  }
  if (strcmp(entity, "log_dump") == 0) {
    EDK_LOGI(kTag, "dumping %u buffered log lines", (unsigned)logger().count());
    logger().dump(dumpLogLine, nullptr);
    return;
  }
  if (strcmp(entity, "diagnostics_reset") == 0) {
    ble.policy().resetStats();
    controller.resetStats();
    lastError.clear();
    lastErrorWall = 0;
    wifiReconnects = 0;
    EDK_LOGI(kTag, "diagnostic counters reset");
    publishDiagnostics(true);
    return;
  }
  if (strcmp(entity, "restart") == 0) {
    if (strcmp(payload, "RESTART") == 0)
      restartWithCause("mqtt restart command");
    return;
  }
  controller.handleUserCommand(entity, payload, now);
}

static void onMqttConnected(void *) {
  DiscoveryContext dc;
  dc.topics = &topics;
  dc.profile = &defaultProfile();
  dc.fwVersion = FW_VERSION;
  dc.stoveMac = ble.foundMac()[0] ? ble.foundMac() : cfg.stoveMac;
  publishDiscovery(mqtt, dc);
  publishAllStates(true);
  publishDiagnostics(true);
}

// ---------------------------------------------------------------------------
// Setup helpers
// ---------------------------------------------------------------------------
// Erases configuration, Wi-Fi credentials and persisted counters, then
// restarts into the setup access point. Used by the boot button and the web UI.
static void factoryReset(const char *origin) {
  EDK_LOGW(kTag, "factory reset (%s)", origin);
  if (mqtt.connected())
    mqtt.publish(topics.make(topicBuf, sizeof(topicBuf), "availability/state"),
                 "OFFLINE", true, true);
  if (LittleFS.begin(true)) {
    store.remove();
    LittleFS.end();
  }
  WiFiManager wm;
  wm.resetSettings();
  prefs.end();
  prefs.begin("edk", false);
  prefs.clear();
  prefs.end();
  Serial.println("Configuration erased, restarting...");
  Serial.flush();
  delay(500);
  ESP.restart();
}

static void factoryResetIfRequested() {
  pinMode(0, INPUT_PULLUP);
  if (digitalRead(0) != LOW)
    return;
  delay(500);
  if (digitalRead(0) != LOW)
    return;
  factoryReset("boot button held");
}

static void loadConfig() {
  if (!LittleFS.begin(true)) {
    EDK_LOGE(kTag, "LittleFS mount failed, using defaults");
    return;
  }
  bool migrated = false;
  bool found = store.load(cfg, migrated);
  char notes[192];
  int fixes = validateConfig(cfg, notes, sizeof(notes));
  if (fixes > 0)
    EDK_LOGW(kTag, "config corrected (%d): %s", fixes, notes);
  if (found && (migrated || fixes > 0))
    store.save(cfg);
}

static void saveConfigCallback() { shouldSaveConfig = true; }

static void runWifiManager() {
  char portBuf[8], sessionBuf[8], pauseBuf[8];
  snprintf(portBuf, sizeof(portBuf), "%u", cfg.mqttPort);
  snprintf(sessionBuf, sizeof(sessionBuf), "%u", cfg.bleSessionS);
  snprintf(pauseBuf, sizeof(pauseBuf), "%u", cfg.blePauseS);

  WiFiManagerParameter pServer("server", "MQTT server", cfg.mqttServer, 63);
  WiFiManagerParameter pPort("port", "MQTT port", portBuf, 6);
  WiFiManagerParameter pUser("user", "MQTT user", cfg.mqttUser, 47);
  WiFiManagerParameter pPass("pass", cfg.mqttPass[0] ? "MQTT password (empty = keep current)" : "MQTT password",
                             "", 63, " type='password'");
  WiFiManagerParameter pNtp("ntp", "NTP server", cfg.ntpServer, 63);
  WiFiManagerParameter pTz("tz", "Timezone (POSIX, e.g. CET-1CEST,M3.5.0,M10.5.0/3)",
                           cfg.timezone, 63);
  WiFiManagerParameter pMac("mac", "Stove BLE MAC (empty = first stove found)",
                            cfg.stoveMac, 17);
  WiFiManagerParameter pSession("ble_session", "BLE session length (s)", sessionBuf, 5);
  WiFiManagerParameter pPause("ble_pause", "BLE pause between sessions (s, 0 = always connected)",
                              pauseBuf, 5);
  WiFiManagerParameter pAdmin("admin_pass",
                              cfg.adminHash[0] ? "Web admin password (8+ chars, empty = keep current)"
                                               : "Web admin password (8+ chars; user 'admin')",
                              "", 63, " type='password'");

  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.addParameter(&pServer);
  wm.addParameter(&pPort);
  wm.addParameter(&pUser);
  wm.addParameter(&pPass);
  wm.addParameter(&pNtp);
  wm.addParameter(&pTz);
  wm.addParameter(&pMac);
  wm.addParameter(&pSession);
  wm.addParameter(&pPause);
  wm.addParameter(&pAdmin);
  wm.setHostname(kHostname);
  wm.setTitle("Edilkamin_BT configuration");
  wm.setConfigPortalTimeout(180);
  wm.setConnectTimeout(30);
  wm.setConnectRetries(3);

  if (!wm.autoConnect("Edilkamin_BT_AP")) {
    // Do not reboot in a loop while the router is down: keep running, the
    // Wi-Fi watchdog in loop() will restart us later if needed.
    EDK_LOGW(kTag, "Wi-Fi not connected after portal timeout");
  } else {
    EDK_LOGI(kTag, "Wi-Fi connected, IP %s RSSI %d",
             WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }

  if (shouldSaveConfig) {
    copyStr(cfg.mqttServer, sizeof(cfg.mqttServer), pServer.getValue());
    cfg.mqttPort = (uint16_t)atoi(pPort.getValue());
    copyStr(cfg.mqttUser, sizeof(cfg.mqttUser), pUser.getValue());
    if (pPass.getValue()[0])
      copyStr(cfg.mqttPass, sizeof(cfg.mqttPass), pPass.getValue());
    copyStr(cfg.ntpServer, sizeof(cfg.ntpServer), pNtp.getValue());
    copyStr(cfg.timezone, sizeof(cfg.timezone), pTz.getValue());
    copyStr(cfg.stoveMac, sizeof(cfg.stoveMac), pMac.getValue());
    cfg.bleSessionS = (uint16_t)atoi(pSession.getValue());
    cfg.blePauseS = (uint16_t)atoi(pPause.getValue());
    trimInPlace(cfg.mqttServer);
    trimInPlace(cfg.timezone);
    char notes[192];
    int fixes = validateConfig(cfg, notes, sizeof(notes));
    if (fixes > 0)
      EDK_LOGW(kTag, "portal values corrected (%d): %s", fixes, notes);
    const char *adminPass = pAdmin.getValue();
    if (adminPass[0]) {
      if (strlen(adminPass) >= 8)
        WebAdmin::setAdminPassword(cfg, cfg.adminUser, adminPass);
      else
        EDK_LOGW(kTag, "portal admin password too short, ignored");
    }
    store.save(cfg);
  }
}

static void setupTime() {
  if (cfg.timezone[0])
    configTzTime(cfg.timezone, cfg.ntpServer);
  else
    configTime(cfg.ntpOffsetSec, 0, cfg.ntpServer);
  // Give NTP a few seconds: BLE frames carry a timestamp.
  uint32_t start = millis();
  while (!timeIsSynced() && millis() - start < 8000)
    delay(100);
  ntpSynced = timeIsSynced();
  if (ntpSynced) {
    char wall[12];
    wallClock(wall, sizeof(wall));
    EDK_LOGI(kTag, "time synced: %s (%s)", wall,
             cfg.timezone[0] ? cfg.timezone : "fixed offset");
  } else {
    EDK_LOGW(kTag, "NTP not synced yet; BLE timestamps will be wrong until it is");
  }
}

static void setupMqtt() {
  char clientId[40];
  snprintf(clientId, sizeof(clientId), "edilkamin_%s", cfg.deviceId);
  char will[96];
  topics.make(will, sizeof(will), "availability/state");
  mqtt.begin(net, cfg.mqttServer, cfg.mqttPort, cfg.mqttUser, cfg.mqttPass,
             clientId, will);
  mqtt.setHandlers(onMqttMessage, onMqttConnected, nullptr);
  static const char *const commandEntities[] = {
      "hvac_mode", "fan_mode", "preset_mode", "target_temperature", "relax",
      "automatic_mode", "chrono_mode", "standby", "bluetooth", "log_level",
      "log_dump", "diagnostics_reset", "restart", "web_admin"};
  char t[96];
  for (const char *e : commandEntities)
    mqtt.addSubscription(topics.set(t, sizeof(t), e), true);
  mqtt.addSubscription("homeassistant/status", false);
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);
  logger().begin(uptimeMs, wallClock);
  logger().addSink(serialSink, nullptr, LogLevel::Trace);
  logger().setLevel(LogLevel::Info);
  EDK_LOGI(kTag, "Edilkamin_BT %s starting (reset: %s)", FW_VERSION,
           resetReasonName(esp_reset_reason()));

  factoryResetIfRequested();

  prefs.begin("edk", false);
  bootCount = prefs.getUInt("boots", 0) + 1;
  prefs.putUInt("boots", bootCount);
  String cause = prefs.getString("cause", "");
  copyStr(lastRestartCause, sizeof(lastRestartCause), cause.c_str());
  if (cause.length())
    prefs.remove("cause");
  EDK_LOGI(kTag, "boot #%lu%s%s", (unsigned long)bootCount,
           cause.length() ? ", last restart cause: " : "", cause.c_str());

  loadConfig();
  logger().setLevel((LogLevel)cfg.logLevel);
  topics.configure(cfg.topicPrefix, cfg.deviceId);

  WiFi.setHostname(kHostname);
  runWifiManager();
  WiFi.setAutoReconnect(true);
  setupTime();

  if (cfg.telnetEnabled) {
    TelnetStream.begin();
    telnetActive = true;
    logger().addSink(telnetSink, nullptr, LogLevel::Trace);
    EDK_LOGW(kTag, "telnet log access enabled (unauthenticated, LAN only)");
  }
  logger().addSink(mqttSink, nullptr, (LogLevel)cfg.mqttLogLevel);

  setupMqtt();
  startWebAdmin();

  StoveController::Config cc;
  cc.pollIntervalMs = cfg.blePollIntervalMs;
  cc.responseTimeoutMs = cfg.bleResponseTimeoutMs;
  cc.commandExpiryMs = (uint32_t)cfg.commandExpiryS * 1000UL;
  cc.dataMaxAgeMs = (uint32_t)cfg.dataMaxAgeS * 1000UL;
  controller.configure(cc);
  controller.setBleEnabled(cfg.bleEnabled);
  g_ctl = &controller;

  BleManager::Callbacks cb;
  cb.onFrame = onBleFrame;
  cb.onStateChange = onBleState;
  ble.begin(cfg, cb);
  ble.setEnabled(cfg.bleEnabled, millis());

  EDK_LOGI(kTag, "setup complete, free heap %lu",
           (unsigned long)ESP.getFreeHeap());
}

static void wifiHousekeeping(uint32_t now) {
  if (WiFi.isConnected()) {
    if (wifiDownSince != 0) {
      EDK_LOGI(kTag, "Wi-Fi back, IP %s", WiFi.localIP().toString().c_str());
      wifiDownSince = 0;
    }
    return;
  }
  if (wifiDownSince == 0) {
    wifiDownSince = now ? now : 1;
    lastWifiRetryAt = now;
    EDK_LOGW(kTag, "Wi-Fi disconnected");
    return;
  }
  if ((uint32_t)(now - lastWifiRetryAt) >= 30000UL) {
    lastWifiRetryAt = now;
    wifiReconnects++;
    EDK_LOGI(kTag, "Wi-Fi reconnect attempt");
    WiFi.reconnect();
  }
  if (cfg.wifiRebootAfterMin &&
      (uint32_t)(now - wifiDownSince) >= (uint32_t)cfg.wifiRebootAfterMin * 60000UL) {
    restartWithCause("wifi watchdog");
  }
}

void loop() {
  uint32_t now = millis();

  if (telnetActive) {
    int c = TelnetStream.read();
    if (c == 'c') {
      TelnetStream.println("Bye...");
      TelnetStream.flush();
      TelnetStream.stop();
    }
  }

  wifiHousekeeping(now);
  mqtt.loop(now, WiFi.isConnected());
  logger().tick(now);
  if (!web.running() && cfg.webEnabled && WiFi.isConnected())
    startWebAdmin();
  web.loop(now);

  if (!ntpSynced && timeIsSynced()) {
    ntpSynced = true;
    EDK_LOGI(kTag, "time synced");
  }

  ble.setUserPending(controller.hasPendingUserCommand());
  ble.setBusy(controller.busy());
  ble.loop(now);
  now = millis(); // BLE connection and discovery can block for several seconds.
  controller.tick(now);

  if (cfg.bleRebootAfterMin &&
      ble.policy().unreachableForMs(now) >= (uint32_t)cfg.bleRebootAfterMin * 60000UL) {
    restartWithCause("ble watchdog");
  }

  if (mqtt.connected() &&
      (uint32_t)(now - lastDiagPublishAt) >= (uint32_t)cfg.diagIntervalS * 1000UL) {
    publishDiagnostics(false);
  }
}
