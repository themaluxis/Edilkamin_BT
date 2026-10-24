// Pure BLE session state machine (F01). Decides *what* to do; the NimBLE
// glue (BleManager) executes the actions and feeds events back.
//
//   DISABLED ── enable ──▶ IDLE ── due/user ──▶ SCANNING ── found ──▶ CONNECTING
//      ▲                     ▲                     │ timeout            │ ok / fail
//      │                     │ session over        ▼                    ▼
//      └── disable ── any    └──────────── COMMUNICATING ◀─ 1st data ─ CONNECTED
//                                               │ drop                 │ no data
//                                               ▼                      ▼
//                                             ERROR ◀───────────────────┘
//                                               │ backoff elapsed → SCANNING
#pragma once
#include <cstdint>

namespace edk {

enum class BleState : uint8_t {
  Disabled = 0,
  Idle,
  Scanning,
  Connecting,
  Connected,
  Communicating,
  Error,
};

enum class BleAction : uint8_t { None = 0, StartScan, StopScan, Connect, Disconnect };

enum class BleErrorReason : uint8_t {
  None = 0,
  NotFound,
  ConnectFailed,
  ServiceMissing,
  NoData,
  LinkDropped,
};

const char *bleStateName(BleState s);
const char *bleErrorReasonName(BleErrorReason r);

struct BlePolicyConfig {
  uint32_t sessionMs = 30000;          // active session length
  uint32_t pauseMs = 120000;           // pause between sessions (0 = continuous)
  uint32_t reconnectInitialMs = 5000;  // first retry delay after an error
  uint32_t reconnectMaxMs = 120000;    // cap for the exponential backoff
  uint32_t scanTimeoutMs = 10000;      // give up scanning after this
  uint32_t connectedNoDataMs = 15000;  // connected but silent → error
  uint32_t minGapMs = 3000;            // min pause before a user-triggered session
};

struct BlePolicyStats {
  uint32_t connectAttempts = 0;
  uint32_t sessionsOk = 0;   // reached Communicating
  uint32_t linkDrops = 0;    // unexpected disconnects
  uint32_t scanNotFound = 0;
  uint32_t connectFailures = 0;
  uint32_t consecutiveErrors = 0;
};

class BleSessionPolicy {
public:
  void configure(const BlePolicyConfig &cfg) { cfg_ = cfg; }
  const BlePolicyConfig &config() const { return cfg_; }

  // --- inputs ---
  void setEnabled(bool enabled, uint32_t now);
  void setUserPending(bool pending) { userPending_ = pending; }
  void setBusy(bool busy) { busy_ = busy; } // a request is in flight
  void requestSession(uint32_t now);         // a user command arrived
  void onScanFound(uint32_t now);
  void onScanFinished(uint32_t now); // scan ended without result
  void onConnectResult(bool ok, BleErrorReason failReason, uint32_t now);
  void onLinkLost(uint32_t now);
  void onValidResponse(uint32_t now);

  // --- tick: returns at most one action to perform ---
  BleAction tick(uint32_t now);

  // --- observability ---
  BleState state() const { return state_; }
  const char *stateName() const { return bleStateName(state_); }
  bool enabled() const { return enabled_; }
  bool linkUp() const {
    return state_ == BleState::Connected || state_ == BleState::Communicating;
  }
  BleErrorReason lastError() const { return lastError_; }
  const BlePolicyStats &stats() const { return stats_; }
  void resetStats() { stats_ = BlePolicyStats(); }
  void stats_connectAttempt() { stats_.connectAttempts++; }
  uint32_t currentBackoffMs() const { return backoffMs_; }
  // ms since the last successful communication (0 when never / disabled)
  uint32_t unreachableForMs(uint32_t now) const;
  bool everCommunicated() const { return lastCommunicatingAt_ != 0 || hadData_; }

private:
  void enter(BleState s, uint32_t now);
  void fail(BleErrorReason r, uint32_t now);
  uint32_t nextBackoff();

  BlePolicyConfig cfg_;
  BleState state_ = BleState::Disabled;
  BleErrorReason lastError_ = BleErrorReason::None;
  BlePolicyStats stats_;
  bool enabled_ = false;
  bool userPending_ = false;
  bool busy_ = false;
  bool expectDisconnect_ = false;
  bool hadData_ = false;
  uint32_t stateSince_ = 0;
  uint32_t nextSessionAt_ = 0;
  uint32_t lastSessionEndAt_ = 0;
  uint32_t retryAt_ = 0;
  uint32_t backoffMs_ = 0;
  uint32_t lastCommunicatingAt_ = 0;
  uint32_t lastValidResponseAt_ = 0;
  uint32_t enabledAt_ = 0;
};

} // namespace edk
