#include "WebAdmin.h"
#include "config/ConfigJson.h"
#include "core/Strings.h"
#include "diagnostics/Diagnostics.h"
#include "logging/Logger.h"
#include "web/WebAssets.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_system.h>

namespace edk {

static const char *kTag = "WEB";
static const size_t kMaxBody = 6144;
static char g_buf[3072];

// ---------------------------------------------------------------------------
void WebAdmin::setAdminPassword(AppConfig &cfg, const char *user,
                                const char *password) {
  copyStr(cfg.adminUser, sizeof(cfg.adminUser), user);
  digestHa1(cfg.adminUser, kAuthRealm, password, cfg.adminHash);
}

void WebAdmin::begin(AppConfig &cfg, ConfigStore &store, const Hooks &hooks,
                     const char *fwVersion) {
  cfg_ = &cfg;
  store_ = &store;
  hooks_ = hooks;
  fwVersion_ = fwVersion;
  nonces_.begin(esp_random(), esp_random());

  static const char *kHeaders[] = {"If-None-Match"}; // + Authorization (implicit)
  server_.collectHeaders(kHeaders, 1);
  server_.enableDelay(false); // never stall the main loop when idle
  server_.on("/", HTTP_GET, [this] { handleIndex(); });
  server_.on("/index.html", HTTP_GET, [this] { handleIndex(); });
  server_.on("/api/bootstrap", HTTP_GET, [this] { handleBootstrap(); });
  server_.on("/api/setup", HTTP_POST, [this] { handleSetup(); });
  server_.on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server_.on("/api/config", HTTP_GET, [this] { handleConfigGet(); });
  server_.on("/api/config", HTTP_POST, [this] { handleConfigPost(); });
  server_.on("/api/config/export", HTTP_GET, [this] { handleExport(); });
  server_.on("/api/log", HTTP_GET, [this] { handleLog(); });
  server_.on("/api/log/level", HTTP_POST, [this] { handleLogLevel(); });
  server_.on("/api/restart", HTTP_POST, [this] { handleRestart(); });
  server_.on("/api/factory-reset", HTTP_POST, [this] { handleFactoryReset(); });
  server_.on("/api/wifi", HTTP_POST, [this] { handleWifi(); });
  server_.on("/api/wifi/scan", HTTP_GET, [this] { handleWifiScan(); });
  server_.on("/api/update/info", HTTP_GET, [this] { handleUpdateInfo(); });
  server_.on("/api/update", HTTP_POST, [this] { handleUpdateDone(); },
             [this] { handleUpdateUpload(); });
  server_.on("/api/update/rollback", HTTP_POST, [this] { handleRollback(); });
  server_.onNotFound([this] { handleNotFound(); });
  server_.begin();
  running_ = true;
  EDK_LOGI(kTag, "web administration on http://%s/ (%s)",
           WiFi.localIP().toString().c_str(),
           setupRequired() ? "password not set yet" : "digest auth");
}

void WebAdmin::stop() {
  if (!running_)
    return;
  server_.stop();
  running_ = false;
  EDK_LOGW(kTag, "web administration stopped");
}

void WebAdmin::loop(uint32_t now) {
  if (running_)
    server_.handleClient();
  if (pending_ == Pending::None || (int32_t)(now - pendingAt_) < 0)
    return;
  Pending p = pending_;
  pending_ = Pending::None;
  switch (p) {
  case Pending::Restart:
    if (hooks_.restart)
      hooks_.restart("web restart", hooks_.ctx);
    break;
  case Pending::FactoryReset:
    if (hooks_.factoryReset)
      hooks_.factoryReset(hooks_.ctx);
    break;
  case Pending::Wifi:
    if (hooks_.setWifi)
      hooks_.setWifi(pendingSsid_, pendingPass_, hooks_.ctx);
    memset(pendingPass_, 0, sizeof(pendingPass_));
    break;
  case Pending::Stop:
    stop();
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
const char *WebAdmin::methodName() {
  switch (server_.method()) {
  case HTTP_GET:
    return "GET";
  case HTTP_POST:
    return "POST";
  case HTTP_PUT:
    return "PUT";
  case HTTP_PATCH:
    return "PATCH";
  case HTTP_DELETE:
    return "DELETE";
  case HTTP_OPTIONS:
    return "OPTIONS";
  default:
    return "GET";
  }
}

void WebAdmin::sendJson(int code, const char *json) {
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(code, "application/json", json);
}

void WebAdmin::sendError(int code, const char *error) {
  char esc[160], json[200];
  jsonEscape(error, esc, sizeof(esc));
  snprintf(json, sizeof(json), "{\"error\":\"%s\"}", esc);
  sendJson(code, json);
}

void WebAdmin::requestAuth(bool stale) {
  char nonce[33];
  nonces_.current(millis(), nonce);
  char hdr[160];
  snprintf(hdr, sizeof(hdr),
           "Digest realm=\"%s\", qop=\"auth\", algorithm=MD5, nonce=\"%s\"%s",
           kAuthRealm, nonce, stale ? ", stale=true" : "");
  server_.sendHeader("WWW-Authenticate", hdr);
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(401, "application/json", "{\"error\":\"unauthorized\"}");
}

WebAdmin::Auth WebAdmin::checkAuth(uint32_t now) {
  if (throttle_.locked(now))
    return Auth::Throttled;
  if (setupRequired())
    return Auth::SetupRequired;
  String hdr = server_.header("Authorization");
  if (hdr.length() == 0)
    return Auth::Missing;
  DigestRequest req;
  if (!parseDigestHeader(hdr.c_str(), req))
    return Auth::Missing;
  if (!nonces_.accepts(req.nonce, now))
    return Auth::Stale; // expired nonce: the browser retries transparently
  String path = server_.uri();
  if (!digestVerify(cfg_->adminHash, cfg_->adminUser, kAuthRealm, methodName(),
                    path.c_str(), req)) {
    throttle_.onFailure(now);
    authFailures_++;
    EDK_LOGW(kTag, "auth failed from %s for %s %s",
             server_.client().remoteIP().toString().c_str(), methodName(),
             path.c_str());
    return Auth::Bad;
  }
  throttle_.onSuccess();
  return Auth::Ok;
}

void WebAdmin::respondAuth(Auth a, uint32_t now) {
  switch (a) {
  case Auth::Throttled: {
    char json[64];
    snprintf(json, sizeof(json), "{\"error\":\"too many attempts\",\"retry_after_s\":%lu}",
             (unsigned long)throttle_.retryAfterS(now));
    sendJson(429, json);
    break;
  }
  case Auth::SetupRequired:
    sendError(403, "setup_required");
    break;
  case Auth::Stale:
    requestAuth(true);
    break;
  case Auth::Ok:
    break;
  default:
    requestAuth(false);
    break;
  }
}

bool WebAdmin::authorize() {
  uint32_t now = millis();
  Auth a = checkAuth(now);
  if (a == Auth::Ok)
    return true;
  respondAuth(a, now);
  return false;
}

bool WebAdmin::readBody(JsonDocument &doc) {
  if (!server_.hasArg("plain")) {
    sendError(400, "missing JSON body");
    return false;
  }
  const String &body = server_.arg("plain");
  if (body.length() > kMaxBody) {
    sendError(413, "body too large");
    return false;
  }
  DeserializationError err = deserializeJson(doc, body);
  if (err || !doc.is<JsonObject>()) {
    sendError(400, "invalid JSON");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------
void WebAdmin::handleIndex() {
  if (server_.hasHeader("If-None-Match") &&
      server_.header("If-None-Match") == kIndexHtmlEtag) {
    server_.send(304, "text/plain", "");
    return;
  }
  server_.sendHeader("Content-Encoding", "gzip");
  server_.sendHeader("Cache-Control", "no-cache");
  server_.sendHeader("ETag", kIndexHtmlEtag);
  server_.sendHeader("X-Frame-Options", "DENY");
  server_.send_P(200, "text/html; charset=utf-8", (PGM_P)kIndexHtmlGz,
                 kIndexHtmlGzSize);
}

void WebAdmin::handleBootstrap() {
  // Unauthenticated on purpose: no secret, no stove data.
  char json[200];
  snprintf(json, sizeof(json),
           "{\"setup_required\":%s,\"fw\":\"%s\",\"realm\":\"%s\","
           "\"log_level\":\"%s\",\"log_level_temporary\":%s}",
           setupRequired() ? "true" : "false", fwVersion_, kAuthRealm,
           logLevelName(logger().level()),
           logger().temporary() ? "true" : "false");
  sendJson(200, json);
}

void WebAdmin::handleSetup() {
  if (!setupRequired()) {
    sendError(403, "password already set; change it from Settings");
    return;
  }
  JsonDocument doc;
  if (!readBody(doc))
    return;
  char user[sizeof(AppConfig::adminUser)];
  copyStr(user, sizeof(user), doc["user"] | "admin");
  trimInPlace(user);
  const char *pass = doc["password"] | "";
  size_t ul = strlen(user), pl = strlen(pass);
  if (ul == 0 || ul > 31) {
    sendError(400, "user must be 1 to 31 characters");
    return;
  }
  if (pl < 8 || pl > 63) {
    sendError(400, "password must be 8 to 63 characters");
    return;
  }
  AppConfig next = *cfg_;
  setAdminPassword(next, user, pass);
  validateConfig(next);
  if (strcmp(next.adminUser, user) != 0) {
    sendError(400, "user contains invalid characters");
    return;
  }
  *cfg_ = next;
  if (!store_->save(*cfg_)) {
    cfg_->adminHash[0] = '\0';
    sendError(500, "could not save configuration");
    return;
  }
  EDK_LOGI(kTag, "administrator password set from %s",
           server_.client().remoteIP().toString().c_str());
  sendJson(200, "{\"ok\":true}");
}

void WebAdmin::handleStatus() {
  if (!authorize())
    return;
  size_t n = hooks_.statusJson ? hooks_.statusJson(g_buf, sizeof(g_buf), hooks_.ctx) : 0;
  if (n == 0 || n >= sizeof(g_buf)) {
    sendError(500, "status unavailable");
    return;
  }
  sendJson(200, g_buf);
}

void WebAdmin::handleConfigGet() {
  if (!authorize())
    return;
  JsonDocument doc;
  configToJson(*cfg_, doc.to<JsonObject>(), ConfigSecrets::Omit);
  doc["has_mqtt_pass"] = cfg_->mqttPass[0] != '\0';
  doc["restart_pending"] = restartPending_;
  doc["fw"] = fwVersion_;
  size_t n = serializeJson(doc, g_buf, sizeof(g_buf));
  if (n >= sizeof(g_buf) - 1) {
    sendError(500, "config too large");
    return;
  }
  sendJson(200, g_buf);
}

void WebAdmin::handleExport() {
  if (!authorize())
    return;
  JsonDocument doc;
  configToJson(*cfg_, doc.to<JsonObject>(), ConfigSecrets::Omit);
  doc["exported_by"] = fwVersion_;
  size_t n = serializeJsonPretty(doc, g_buf, sizeof(g_buf));
  if (n >= sizeof(g_buf) - 1) {
    sendError(500, "config too large");
    return;
  }
  server_.sendHeader("Content-Disposition",
                     "attachment; filename=\"edilkamin-config.json\"");
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(200, "application/json", g_buf);
}

void WebAdmin::handleConfigPost() {
  if (!authorize())
    return;
  JsonDocument doc;
  if (!readBody(doc))
    return;
  JsonObject obj = doc.as<JsonObject>();

  // Plaintext secrets are handled here and never reach the codec.
  char mqttPass[sizeof(AppConfig::mqttPass)] = "";
  char adminPass[64] = "";
  if (obj["mqtt_pass"].is<const char *>())
    copyStr(mqttPass, sizeof(mqttPass), obj["mqtt_pass"].as<const char *>());
  if (obj["admin_pass"].is<const char *>())
    copyStr(adminPass, sizeof(adminPass), obj["admin_pass"].as<const char *>());
  obj.remove("mqtt_pass");
  obj.remove("admin_pass");
  obj.remove("admin_hash");

  AppConfig next = *cfg_;
  ConfigApplyResult r = configFromJson(obj, next, false);
  if (mqttPass[0])
    copyStr(next.mqttPass, sizeof(next.mqttPass), mqttPass);

  // Normalise first so the password hash is derived from the stored user.
  char requestedUser[sizeof(AppConfig::adminUser)];
  copyStr(requestedUser, sizeof(requestedUser), next.adminUser);
  char notes[192];
  int fixes = validateConfig(next, notes, sizeof(notes));
  trimInPlace(requestedUser);
  if (strcmp(next.adminUser, requestedUser) != 0) {
    sendError(400, "admin_user contains invalid characters");
    return;
  }
  bool userChanged = strcmp(next.adminUser, cfg_->adminUser) != 0;
  if (adminPass[0]) {
    size_t pl = strlen(adminPass);
    if (pl < 8 || pl > 63) {
      sendError(400, "admin_pass must be 8 to 63 characters");
      return;
    }
    setAdminPassword(next, next.adminUser, adminPass);
  } else if (userChanged) {
    sendError(400, "admin_pass is required when changing admin_user");
    return;
  }
  memset(adminPass, 0, sizeof(adminPass));

  bool restart = false;
  char why[96] = "";
  if (hooks_.applyConfig &&
      !hooks_.applyConfig(next, restart, why, sizeof(why), hooks_.ctx)) {
    sendError(400, why[0] ? why : "configuration refused");
    return;
  }
  bool webStopping = !next.webEnabled;
  *cfg_ = next;
  if (!store_->save(*cfg_)) {
    sendError(500, "could not save configuration");
    return;
  }
  if (restart)
    restartPending_ = true;
  EDK_LOGI(kTag, "configuration updated via web (%d keys%s)", r.applied,
           restart ? ", restart required" : "");

  JsonDocument out;
  out["ok"] = true;
  out["applied"] = r.applied;
  out["ignored"] = r.ignored;
  out["unknown"] = r.unknown;
  out["fixes"] = fixes;
  out["notes"] = notes;
  out["restart_required"] = restartPending_;
  out["web_stopping"] = webStopping;
  serializeJson(out, g_buf, sizeof(g_buf));
  sendJson(200, g_buf);
  if (webStopping) {
    pending_ = Pending::Stop;
    pendingAt_ = millis() + 300;
  }
}

static void logLineOut(const char *line, void *ctx) {
  WebServer *s = static_cast<WebServer *>(ctx);
  s->sendContent(line);
  s->sendContent("\n");
}

void WebAdmin::handleLog() {
  if (!authorize())
    return;
  server_.sendHeader("Cache-Control", "no-store");
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "text/plain; charset=utf-8", "");
  logger().dump(logLineOut, &server_);
  server_.sendContent("");
}

void WebAdmin::handleLogLevel() {
  if (!authorize())
    return;
  JsonDocument doc;
  if (!readBody(doc))
    return;
  const char *name = doc["level"] | "";
  LogLevel level;
  if (!parseLogLevel(name, level)) {
    sendError(400, "unknown level");
    return;
  }
  if (hooks_.setLogLevel)
    hooks_.setLogLevel(level, hooks_.ctx);
  else
    logger().setLevel(level);
  char json[96];
  snprintf(json, sizeof(json), "{\"ok\":true,\"level\":\"%s\",\"temporary\":%s}",
           logLevelName(logger().level()), logger().temporary() ? "true" : "false");
  sendJson(200, json);
}

void WebAdmin::handleRestart() {
  if (!authorize())
    return;
  EDK_LOGW(kTag, "restart requested from %s",
           server_.client().remoteIP().toString().c_str());
  sendJson(200, "{\"ok\":true}");
  pending_ = Pending::Restart;
  pendingAt_ = millis() + 500;
}

void WebAdmin::handleFactoryReset() {
  if (!authorize())
    return;
  JsonDocument doc;
  if (!readBody(doc))
    return;
  const char *confirm = doc["confirm"] | "";
  if (strcmp(confirm, "ERASE") != 0) {
    sendError(400, "confirmation text must be ERASE");
    return;
  }
  EDK_LOGW(kTag, "factory reset requested from %s",
           server_.client().remoteIP().toString().c_str());
  sendJson(200, "{\"ok\":true}");
  pending_ = Pending::FactoryReset;
  pendingAt_ = millis() + 500;
}

void WebAdmin::handleWifi() {
  if (!authorize())
    return;
  JsonDocument doc;
  if (!readBody(doc))
    return;
  const char *ssid = doc["ssid"] | "";
  const char *pass = doc["password"] | "";
  size_t sl = strlen(ssid), pl = strlen(pass);
  if (sl == 0 || sl > 32) {
    sendError(400, "ssid must be 1 to 32 characters");
    return;
  }
  if (pl != 0 && (pl < 8 || pl > 63)) {
    sendError(400, "password must be empty or 8 to 63 characters");
    return;
  }
  copyStr(pendingSsid_, sizeof(pendingSsid_), ssid);
  copyStr(pendingPass_, sizeof(pendingPass_), pass);
  EDK_LOGW(kTag, "Wi-Fi change to '%s' requested from %s", ssid,
           server_.client().remoteIP().toString().c_str());
  sendJson(200, "{\"ok\":true}");
  pending_ = Pending::Wifi;
  pendingAt_ = millis() + 500;
}

void WebAdmin::handleWifiScan() {
  if (!authorize())
    return;
  int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    sendJson(200, "{\"status\":\"scanning\"}");
    return;
  }
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true, false, false, 120);
    sendJson(200, "{\"status\":\"scanning\"}");
    return;
  }
  JsonDocument doc;
  doc["status"] = "done";
  JsonArray arr = doc["networks"].to<JsonArray>();
  for (int16_t i = 0; i < n && i < 20; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  size_t len = serializeJson(doc, g_buf, sizeof(g_buf));
  if (len >= sizeof(g_buf) - 1) {
    sendError(500, "too many networks");
    return;
  }
  sendJson(200, g_buf);
}

// ---------------------------------------------------------------------------
// OTA
// ---------------------------------------------------------------------------
void WebAdmin::handleUpdateInfo() {
  if (!authorize())
    return;
  if (!ota_) {
    sendError(500, "OTA not available");
    return;
  }
  OtaManager::Info i = ota_->info();
  JsonDocument doc;
  doc["running"] = i.running;
  doc["running_size"] = i.runningSize;
  doc["running_state"] = i.runningState;
  doc["next"] = i.next;
  doc["next_size"] = i.nextSize;
  doc["other_state"] = i.otherState;
  doc["boot"] = i.boot;
  doc["pending_verify"] = i.pendingVerify;
  doc["can_rollback"] = i.canRollBack;
  doc["ota_capable"] = ota_->otaCapable();
  doc["sketch_size"] = i.sketchSize;
  doc["free_sketch"] = i.freeSketch;
  doc["fw"] = fwVersion_;
  serializeJson(doc, g_buf, sizeof(g_buf));
  sendJson(200, g_buf);
}

// Called for every chunk of the multipart body. No response may be sent from
// here; the outcome is reported by handleUpdateDone().
void WebAdmin::handleUpdateUpload() {
  HTTPUpload &up = server_.upload();
  char err[128];
  switch (up.status) {
  case UPLOAD_FILE_START:
    uploadError_[0] = '\0';
    uploadAuth_ = checkAuth(millis());
    if (uploadAuth_ != Auth::Ok) {
      upload_ = UploadState::Denied;
      return;
    }
    if (!ota_ || !ota_->start(err, sizeof(err))) {
      copyStr(uploadError_, sizeof(uploadError_), ota_ ? err : "OTA not available");
      upload_ = UploadState::Failed;
      return;
    }
    EDK_LOGW(kTag, "firmware upload '%s' from %s", up.filename.c_str(),
             server_.client().remoteIP().toString().c_str());
    upload_ = UploadState::Writing;
    return;
  case UPLOAD_FILE_WRITE:
    if (upload_ != UploadState::Writing)
      return; // denied or already failed: drain silently
    if (!ota_->write(up.buf, up.currentSize, err, sizeof(err))) {
      copyStr(uploadError_, sizeof(uploadError_), err);
      upload_ = UploadState::Failed;
    }
    return;
  case UPLOAD_FILE_END:
    if (upload_ != UploadState::Writing)
      return;
    if (ota_->finish(err, sizeof(err)))
      upload_ = UploadState::Done;
    else {
      copyStr(uploadError_, sizeof(uploadError_), err);
      upload_ = UploadState::Failed;
    }
    return;
  case UPLOAD_FILE_ABORTED:
    if (ota_)
      ota_->abort();
    copyStr(uploadError_, sizeof(uploadError_), "upload aborted by the client");
    upload_ = UploadState::Failed;
    return;
  }
}

void WebAdmin::handleUpdateDone() {
  UploadState st = upload_;
  upload_ = UploadState::Idle;
  switch (st) {
  case UploadState::Denied:
    respondAuth(uploadAuth_, millis());
    return;
  case UploadState::Done: {
    JsonDocument doc;
    doc["ok"] = true;
    doc["size"] = ota_->written();
    doc["version"] = ota_->imageVersion();
    doc["restarting"] = true;
    serializeJson(doc, g_buf, sizeof(g_buf));
    sendJson(200, g_buf);
    pending_ = Pending::Restart;
    pendingAt_ = millis() + 1500;
    return;
  }
  case UploadState::Failed:
    sendError(400, uploadError_[0] ? uploadError_ : "update failed");
    return;
  default:
    // multipart body without a file part, or authorisation never checked
    if (!authorize())
      return;
    sendError(400, "no firmware file in the request (field name: firmware)");
    return;
  }
}

void WebAdmin::handleRollback() {
  if (!authorize())
    return;
  char err[128];
  if (!ota_ || !ota_->rollBack(err, sizeof(err))) {
    sendError(400, ota_ ? err : "OTA not available");
    return;
  }
  EDK_LOGW(kTag, "rollback requested from %s",
           server_.client().remoteIP().toString().c_str());
  sendJson(200, "{\"ok\":true,\"restarting\":true}");
  pending_ = Pending::Restart;
  pendingAt_ = millis() + 500;
}

void WebAdmin::handleNotFound() {
  if (server_.uri().startsWith("/api/"))
    sendError(404, "not found");
  else
    server_.send(404, "text/plain", "Not found");
}

} // namespace edk
