// Local web administration (F05): status, settings, log, maintenance.
// Platform layer around the Arduino WebServer; the authentication logic lives
// in web/DigestAuth (pure) and the configuration codec in config/ConfigJson.
//
// Security model
//  * HTTP Digest authentication on everything except the static page,
//    /api/bootstrap and /api/setup (first password). Only HA1 is stored.
//  * Administration is locked until a password has been chosen.
//  * Brute-force throttle (5 failures/min -> 30 s lockout).
//  * Secrets never leave the device: GET/export omit them, import ignores them.
//  * The whole service can be disabled (web_enabled = false).
#pragma once
#include "config/AppConfig.h"
#include "config/ConfigStore.h"
#include "core/Log.h"
#include "ota/OtaManager.h"
#include "web/DigestAuth.h"
#include <ArduinoJson.h>
#include <WebServer.h>

namespace edk {

class WebAdmin {
public:
  struct Hooks {
    void *ctx = nullptr;
    // Writes the status document (JSON). Returns the length that would have
    // been written (snprintf semantics).
    size_t (*statusJson)(char *buf, size_t n, void *ctx) = nullptr;
    // Called with the validated candidate before it is saved. Return false
    // with a message to refuse; set restartRequired when a change cannot be
    // applied live. The hook applies live changes itself.
    bool (*applyConfig)(const AppConfig &next, bool &restartRequired, char *msg,
                        size_t msgLen, void *ctx) = nullptr;
    void (*setLogLevel)(LogLevel level, void *ctx) = nullptr;
    // Deferred actions, invoked from loop() after the response went out.
    void (*restart)(const char *cause, void *ctx) = nullptr;
    void (*factoryReset)(void *ctx) = nullptr;
    void (*setWifi)(const char *ssid, const char *pass, void *ctx) = nullptr;
  };

  void begin(AppConfig &cfg, ConfigStore &store, const Hooks &hooks,
             const char *fwVersion);
  void setOta(OtaManager *ota) { ota_ = ota; }
  void loop(uint32_t now);
  void stop();
  bool running() const { return running_; }
  bool setupRequired() const { return cfg_ && cfg_->adminHash[0] == '\0'; }
  bool restartPending() const { return restartPending_; }
  void setRestartPending(bool v) { restartPending_ = v; }
  uint32_t authFailures() const { return authFailures_; }

  // Stores HA1 for (user, password); the plaintext is never kept.
  static void setAdminPassword(AppConfig &cfg, const char *user,
                               const char *password);

private:
  enum class Pending : uint8_t { None, Restart, FactoryReset, Wifi, Stop };
  enum class Auth : uint8_t { Ok, Throttled, SetupRequired, Missing, Stale, Bad };
  enum class UploadState : uint8_t { Idle, Denied, Writing, Done, Failed };

  Auth checkAuth(uint32_t now);
  void respondAuth(Auth a, uint32_t now);
  bool authorize();
  void sendJson(int code, const char *json);
  void sendError(int code, const char *error);
  void requestAuth(bool stale);
  const char *methodName();
  bool readBody(JsonDocument &doc);

  void handleIndex();
  void handleBootstrap();
  void handleSetup();
  void handleStatus();
  void handleConfigGet();
  void handleConfigPost();
  void handleExport();
  void handleLog();
  void handleLogLevel();
  void handleRestart();
  void handleFactoryReset();
  void handleWifi();
  void handleWifiScan();
  void handleUpdateInfo();
  void handleUpdateUpload();
  void handleUpdateDone();
  void handleRollback();
  void handleNotFound();

  WebServer server_{80};
  AppConfig *cfg_ = nullptr;
  ConfigStore *store_ = nullptr;
  Hooks hooks_;
  const char *fwVersion_ = "dev";
  NonceSource nonces_;
  AuthThrottle throttle_;
  bool running_ = false;
  bool restartPending_ = false;
  uint32_t authFailures_ = 0;
  Pending pending_ = Pending::None;
  uint32_t pendingAt_ = 0;
  char pendingSsid_[33] = "";
  char pendingPass_[64] = "";
  OtaManager *ota_ = nullptr;
  UploadState upload_ = UploadState::Idle;
  Auth uploadAuth_ = Auth::Missing;
  char uploadError_[128] = "";
};

} // namespace edk
