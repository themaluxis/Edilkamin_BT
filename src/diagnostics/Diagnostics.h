// Diagnostics snapshot → JSON for the MQTT diagnostics topic (F04). Pure.
#pragma once
#include <cstddef>
#include <cstdint>

namespace edk {

struct DiagSnapshot {
  const char *firmware = "dev";
  const char *bleState = "DISABLED";
  bool bleConnected = false;
  bool bleEnabled = true;
  const char *stoveMac = "";
  uint32_t bleConnectAttempts = 0;
  uint32_t bleSessionsOk = 0;
  uint32_t bleDisconnects = 0;
  uint32_t bleScanNotFound = 0;
  uint32_t requestsOk = 0;
  uint32_t requestsFailed = 0;
  uint32_t framesInvalid = 0;
  uint32_t commandsRejected = 0;
  uint32_t commandsFailed = 0;
  uint32_t queueDepth = 0;
  uint32_t queueCapacity = 0;
  uint32_t queueOverflows = 0;
  uint32_t mqttReconnects = 0;
  uint32_t wifiReconnects = 0;
  int8_t wifiRssi = 0;
  const char *ip = "";
  uint32_t uptimeS = 0;
  uint32_t freeHeap = 0;
  uint32_t minFreeHeap = 0;
  uint32_t bootCount = 0;
  const char *resetReason = "";
  const char *lastRestartCause = "";
  const char *lastError = "";
  const char *lastErrorAt = ""; // ISO timestamp or ""
  const char *lastValidAt = ""; // ISO timestamp or ""
  bool dataValid = false;
  bool ntpSynced = false;
  const char *logLevel = "INFO";
};

// Escapes a string for use inside a JSON string literal. Returns chars written.
size_t jsonEscape(const char *in, char *out, size_t n);

// Writes the JSON document into buf. Returns the number of chars that would
// have been written (like snprintf); check against n for truncation.
size_t buildDiagnosticsJson(const DiagSnapshot &s, char *buf, size_t n);

// Last-error bookkeeping shared by the platform layer.
class ErrorTracker {
public:
  static constexpr size_t kLen = 96;
  void set(const char *msg, uint32_t uptimeMs);
  void clear() { msg_[0] = '\0'; at_ = 0; }
  const char *message() const { return msg_; }
  uint32_t at() const { return at_; }
  bool has() const { return msg_[0] != '\0'; }

private:
  char msg_[kLen] = "";
  uint32_t at_ = 0;
};

} // namespace edk
