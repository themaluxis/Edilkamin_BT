#include "Diagnostics.h"
#include "core/Strings.h"
#include <cstdio>
#include <cstring>

namespace edk {

// Escape a string for JSON (quotes, backslashes, control chars).
size_t jsonEscape(const char *in, char *out, size_t n) {
  size_t o = 0;
  for (const char *p = in ? in : ""; *p; p++) {
    const char *rep = nullptr;
    char tmp[8];
    switch (*p) {
    case '"':
      rep = "\\\"";
      break;
    case '\\':
      rep = "\\\\";
      break;
    case '\n':
      rep = "\\n";
      break;
    case '\r':
      rep = "\\r";
      break;
    case '\t':
      rep = "\\t";
      break;
    default:
      if ((unsigned char)*p < 0x20) {
        snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned char)*p);
        rep = tmp;
      }
    }
    if (rep) {
      size_t l = strlen(rep);
      if (o + l >= n)
        break;
      memcpy(out + o, rep, l);
      o += l;
    } else {
      if (o + 1 >= n)
        break;
      out[o++] = *p;
    }
  }
  if (n)
    out[o < n ? o : n - 1] = '\0';
  return o;
}

size_t buildDiagnosticsJson(const DiagSnapshot &s, char *buf, size_t n) {
  char err[2 * ErrorTracker::kLen + 8];
  jsonEscape(s.lastError, err, sizeof(err));
  char cause[64];
  jsonEscape(s.lastRestartCause, cause, sizeof(cause));
  int w = snprintf(
      buf, n,
      "{\"fw\":\"%s\",\"ble_state\":\"%s\",\"ble_connected\":%s,"
      "\"ble_enabled\":%s,\"stove_mac\":\"%s\","
      "\"ble_connect_attempts\":%lu,\"ble_sessions_ok\":%lu,"
      "\"ble_disconnects\":%lu,\"ble_scan_not_found\":%lu,"
      "\"requests_ok\":%lu,\"requests_failed\":%lu,\"frames_invalid\":%lu,"
      "\"commands_rejected\":%lu,\"commands_failed\":%lu,"
      "\"queue_depth\":%lu,\"queue_capacity\":%lu,\"queue_overflows\":%lu,"
      "\"mqtt_reconnects\":%lu,\"wifi_reconnects\":%lu,\"rssi\":%d,"
      "\"ip\":\"%s\",\"uptime_s\":%lu,\"free_heap\":%lu,\"min_free_heap\":%lu,"
      "\"boot_count\":%lu,\"reset_reason\":\"%s\",\"last_restart_cause\":\"%s\","
      "\"last_error\":\"%s\",\"last_error_at\":\"%s\",\"last_valid_at\":\"%s\","
      "\"data_valid\":%s,\"ntp_synced\":%s,\"log_level\":\"%s\","
      "\"ota_slot\":\"%s\",\"ota_pending\":%s}",
      s.firmware, s.bleState, s.bleConnected ? "true" : "false",
      s.bleEnabled ? "true" : "false", s.stoveMac,
      (unsigned long)s.bleConnectAttempts, (unsigned long)s.bleSessionsOk,
      (unsigned long)s.bleDisconnects, (unsigned long)s.bleScanNotFound,
      (unsigned long)s.requestsOk, (unsigned long)s.requestsFailed,
      (unsigned long)s.framesInvalid, (unsigned long)s.commandsRejected,
      (unsigned long)s.commandsFailed, (unsigned long)s.queueDepth,
      (unsigned long)s.queueCapacity, (unsigned long)s.queueOverflows,
      (unsigned long)s.mqttReconnects, (unsigned long)s.wifiReconnects,
      (int)s.wifiRssi, s.ip, (unsigned long)s.uptimeS,
      (unsigned long)s.freeHeap, (unsigned long)s.minFreeHeap,
      (unsigned long)s.bootCount, s.resetReason, cause, err, s.lastErrorAt,
      s.lastValidAt, s.dataValid ? "true" : "false",
      s.ntpSynced ? "true" : "false", s.logLevel, s.otaSlot,
      s.otaPending ? "true" : "false");
  return w < 0 ? 0 : (size_t)w;
}

void ErrorTracker::set(const char *msg, uint32_t uptimeMs) {
  copyStr(msg_, sizeof(msg_), msg);
  at_ = uptimeMs;
}

} // namespace edk
