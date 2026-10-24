#include "BleSessionPolicy.h"
#include "core/Log.h"

namespace edk {

static const char *kTag = "BLE";

const char *bleStateName(BleState s) {
  switch (s) {
  case BleState::Disabled:
    return "DISABLED";
  case BleState::Idle:
    return "IDLE";
  case BleState::Scanning:
    return "SCANNING";
  case BleState::Connecting:
    return "CONNECTING";
  case BleState::Connected:
    return "CONNECTED";
  case BleState::Communicating:
    return "COMMUNICATING";
  case BleState::Error:
    return "ERROR";
  }
  return "?";
}

const char *bleErrorReasonName(BleErrorReason r) {
  switch (r) {
  case BleErrorReason::None:
    return "none";
  case BleErrorReason::NotFound:
    return "stove not found";
  case BleErrorReason::ConnectFailed:
    return "connect failed";
  case BleErrorReason::ServiceMissing:
    return "service/characteristic missing";
  case BleErrorReason::NoData:
    return "connected but no valid data";
  case BleErrorReason::LinkDropped:
    return "link dropped";
  }
  return "?";
}

void BleSessionPolicy::enter(BleState s, uint32_t now) {
  if (s == state_)
    return;
  EDK_LOGI(kTag, "%s -> %s", bleStateName(state_), bleStateName(s));
  state_ = s;
  stateSince_ = now;
}

uint32_t BleSessionPolicy::nextBackoff() {
  if (backoffMs_ == 0)
    backoffMs_ = cfg_.reconnectInitialMs;
  else if (backoffMs_ < cfg_.reconnectMaxMs)
    backoffMs_ = backoffMs_ * 2 > cfg_.reconnectMaxMs ? cfg_.reconnectMaxMs
                                                      : backoffMs_ * 2;
  return backoffMs_;
}

void BleSessionPolicy::fail(BleErrorReason r, uint32_t now) {
  lastError_ = r;
  stats_.consecutiveErrors++;
  retryAt_ = now + nextBackoff();
  EDK_LOGW(kTag, "error: %s (retry in %lu ms, consecutive %lu)",
           bleErrorReasonName(r), (unsigned long)backoffMs_,
           (unsigned long)stats_.consecutiveErrors);
  enter(BleState::Error, now);
}

void BleSessionPolicy::setEnabled(bool enabled, uint32_t now) {
  if (enabled == enabled_ && state_ != BleState::Disabled)
    return;
  enabled_ = enabled;
  if (enabled && state_ == BleState::Disabled) {
    nextSessionAt_ = now; // first session right away
    enabledAt_ = now == 0 ? 1 : now;
    lastCommunicatingAt_ = 0;
    backoffMs_ = 0;
    stats_.consecutiveErrors = 0;
    enter(BleState::Idle, now);
  }
  // Disabling is handled in tick() so the link is torn down cleanly.
}

void BleSessionPolicy::requestSession(uint32_t now) {
  userPending_ = true;
  if (state_ == BleState::Idle) {
    uint32_t earliest = lastSessionEndAt_ + cfg_.minGapMs;
    if ((int32_t)(nextSessionAt_ - earliest) > 0)
      nextSessionAt_ = earliest;
    if ((int32_t)(nextSessionAt_ - now) < 0)
      nextSessionAt_ = now;
  }
}

void BleSessionPolicy::onScanFound(uint32_t now) {
  if (state_ != BleState::Scanning)
    return;
  enter(BleState::Connecting, now);
}

void BleSessionPolicy::onScanFinished(uint32_t now) {
  if (state_ != BleState::Scanning)
    return;
  stats_.scanNotFound++;
  fail(BleErrorReason::NotFound, now);
}

void BleSessionPolicy::onConnectResult(bool ok, BleErrorReason failReason,
                                       uint32_t now) {
  if (state_ != BleState::Connecting)
    return;
  if (ok) {
    hadData_ = false;
    expectDisconnect_ = false;
    enter(BleState::Connected, now);
  } else {
    stats_.connectFailures++;
    fail(failReason == BleErrorReason::None ? BleErrorReason::ConnectFailed
                                            : failReason,
         now);
  }
}

void BleSessionPolicy::onLinkLost(uint32_t now) {
  if (!(state_ == BleState::Connected || state_ == BleState::Communicating ||
        state_ == BleState::Connecting))
    return;
  if (expectDisconnect_) {
    expectDisconnect_ = false;
    return;
  }
  stats_.linkDrops++;
  lastSessionEndAt_ = now;
  fail(BleErrorReason::LinkDropped, now);
}

void BleSessionPolicy::onValidResponse(uint32_t now) {
  lastValidResponseAt_ = now;
  hadData_ = true;
  if (state_ == BleState::Connected) {
    stats_.sessionsOk++;
    stats_.consecutiveErrors = 0;
    backoffMs_ = 0;
    enter(BleState::Communicating, now);
  }
  if (state_ == BleState::Communicating)
    lastCommunicatingAt_ = now;
}

uint32_t BleSessionPolicy::unreachableForMs(uint32_t now) const {
  if (!enabled_ || state_ == BleState::Disabled ||
      state_ == BleState::Communicating)
    return 0;
  // Time since the last successful exchange, or since BLE was enabled when
  // the stove has never answered yet.
  uint32_t since = lastCommunicatingAt_ != 0 ? lastCommunicatingAt_ : enabledAt_;
  if (since == 0)
    return 0;
  return (uint32_t)(now - since);
}

BleAction BleSessionPolicy::tick(uint32_t now) {
  // Disable requested: tear down whatever is going on, then park.
  if (!enabled_ && state_ != BleState::Disabled) {
    BleState prev = state_;
    enter(BleState::Disabled, now);
    userPending_ = false;
    if (prev == BleState::Scanning)
      return BleAction::StopScan;
    if (prev == BleState::Connected || prev == BleState::Communicating ||
        prev == BleState::Connecting) {
      expectDisconnect_ = true;
      return BleAction::Disconnect;
    }
    return BleAction::None;
  }

  switch (state_) {
  case BleState::Disabled:
    return BleAction::None;

  case BleState::Idle:
    if ((int32_t)(now - nextSessionAt_) >= 0) {
      enter(BleState::Scanning, now);
      return BleAction::StartScan;
    }
    return BleAction::None;

  case BleState::Scanning:
    if ((uint32_t)(now - stateSince_) >= cfg_.scanTimeoutMs) {
      stats_.scanNotFound++;
      fail(BleErrorReason::NotFound, now);
      return BleAction::StopScan;
    }
    return BleAction::None;

  case BleState::Connecting:
    // Connect is synchronous on the manager side; nothing to time here.
    return BleAction::None;

  case BleState::Connected:
    if ((uint32_t)(now - stateSince_) >= cfg_.connectedNoDataMs) {
      expectDisconnect_ = true;
      lastSessionEndAt_ = now;
      fail(BleErrorReason::NoData, now);
      return BleAction::Disconnect;
    }
    return BleAction::None;

  case BleState::Communicating: {
    bool continuous = cfg_.pauseMs == 0;
    bool over = (uint32_t)(now - stateSince_) >= cfg_.sessionMs;
    if (!continuous && over && !userPending_ && !busy_) {
      expectDisconnect_ = true;
      lastSessionEndAt_ = now;
      nextSessionAt_ = now + cfg_.pauseMs;
      EDK_LOGI(kTag, "session over, next in %lu s",
               (unsigned long)(cfg_.pauseMs / 1000));
      enter(BleState::Idle, now);
      return BleAction::Disconnect;
    }
    return BleAction::None;
  }

  case BleState::Error:
    if ((int32_t)(now - retryAt_) >= 0) {
      enter(BleState::Scanning, now);
      return BleAction::StartScan;
    }
    return BleAction::None;
  }
  return BleAction::None;
}

} // namespace edk
