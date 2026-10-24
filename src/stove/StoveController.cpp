#include "StoveController.h"
#include "commands/CommandFactory.h"
#include "core/Log.h"
#include "protocol/Decoders.h"
#include <cstdio>
#include <cstring>

namespace edk {

static const char *kTag = "STOVE";

const CommandType StoveController::kPollOrder[StoveController::kPollCount] = {
    CommandType::ReadMainStatus, CommandType::ReadRoomTemp,
    CommandType::ReadTargetTemp, CommandType::ReadFanPower,
    CommandType::ReadAutomatic,  CommandType::ReadThermocouple,
    CommandType::ReadRelax,      CommandType::ReadStandby,
    CommandType::ReadChrono,     CommandType::ReadWarningFlags};

StoveController::StoveController(IStoveLink &link, IStoveEvents &events,
                                 const StoveProfile &profile)
    : link_(link), events_(events), profile_(profile) {}

void StoveController::report(Command &cmd, CommandState st,
                             const char *reason) {
  cmd.state = st;
  if (cmd.source == CommandSource::User || st == CommandState::Verified ||
      st == CommandState::Failed || st == CommandState::Rejected) {
    events_.onCommandResult(cmd, reason ? reason : "");
  }
  if (st == CommandState::Failed || st == CommandState::Rejected ||
      (st == CommandState::Timeout && cmd.source == CommandSource::User)) {
    char msg[96];
    snprintf(msg, sizeof(msg), "%s %s %s: %s", commandTypeName(cmd.type),
             cmd.payload, commandStateName(st), reason ? reason : "");
    events_.onError(msg);
  }
}

void StoveController::handleUserCommand(const char *entity,
                                        const char *payload, uint32_t now) {
  Command cmd;
  char reason[64];
  EDK_LOGI(kTag, "command %s <- '%s'", entity ? entity : "?",
           payload ? payload : "");
  if (!buildUserCommand(entity, payload, state_, profile_, now, cmd, reason,
                        sizeof(reason))) {
    stats_.commandsRejected++;
    cmd.id = queue_.nextId();
    report(cmd, CommandState::Rejected, reason);
    return;
  }
  if (!bleEnabled_) {
    stats_.commandsRejected++;
    cmd.id = queue_.nextId();
    report(cmd, CommandState::Rejected, "bluetooth disabled");
    return;
  }
  enqueueUser(cmd);
}

void StoveController::readRegister(uint16_t reg, uint8_t count, uint32_t now) {
  Command cmd;
  if (!buildRegisterRead(reg, count, now, cmd)) {
    stats_.commandsRejected++;
    cmd.type = CommandType::ReadRegister;
    cmd.source = CommandSource::User;
    snprintf(cmd.payload, sizeof(cmd.payload), "0x%04X,%u", reg, count);
    cmd.id = queue_.nextId();
    report(cmd, CommandState::Rejected, "count must be 1..3");
    return;
  }
  if (!bleEnabled_) {
    stats_.commandsRejected++;
    cmd.id = queue_.nextId();
    report(cmd, CommandState::Rejected, "bluetooth disabled");
    return;
  }
  EDK_LOGI(kTag, "register read 0x%04X x%u", reg, count);
  enqueueUser(cmd);
}

void StoveController::enqueueUser(Command cmd) {
  if (cmd.id == 0)
    cmd.id = queue_.nextId();
  Command superseded;
  CommandQueue::EnqueueResult r = queue_.enqueue(cmd, &superseded);
  if (r == CommandQueue::EnqueueResult::Full) {
    stats_.commandsFailed++;
    report(cmd, CommandState::Failed, "command queue full");
    return;
  }
  if (r == CommandQueue::EnqueueResult::Replaced)
    report(superseded, CommandState::Rejected, "superseded by newer command");
  report(cmd, CommandState::Queued);
  events_.onUserCommandQueued();
}

void StoveController::setLinkReady(bool ready, uint32_t now) {
  if (ready == linkReady_)
    return;
  linkReady_ = ready;
  if (ready) {
    pollIndex_ = 0;
    lastPollAt_ = now - cfg_.pollIntervalMs; // poll immediately
    lastSendAt_ = now - cfg_.interCommandGapMs;
    return;
  }
  // Link went away: settle the in-flight command and drop poll reads.
  if (hasInflight_) {
    hasInflight_ = false;
    Command c = inflight_;
    if (c.isWrite()) {
      stats_.requestsTimeout++;
      report(c, CommandState::Timeout, "link lost before echo");
      scheduleVerify(c, now);
    } else if (c.source == CommandSource::Verify) {
      stats_.requestsTimeout++;
      // keep it for the next session
      c.state = CommandState::Validated;
      queue_.enqueue(c);
    } else if (c.source == CommandSource::User) {
      stats_.requestsTimeout++;
      stats_.commandsFailed++;
      report(c, CommandState::Failed, "link lost before answer");
    } else {
      stats_.requestsTimeout++;
    }
  }
  queue_.removeBySource(CommandSource::Poll);
}

uint8_t StoveController::buildPollList(CommandType *out) const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kPollCount; i++)
    out[n++] = kPollOrder[i];
  if (cfg_.pollExperimental) {
    out[n++] = CommandType::ReadAlarm;
    out[n++] = CommandType::ReadPelletRemaining;
    if (cfg_.fanCount >= 2)
      out[n++] = CommandType::ReadFan23;
    out[n++] = CommandType::ReadControllerVersion;
  }
  return n;
}

void StoveController::enqueuePollIfDue(uint32_t now) {
  if (queue_.hasPending())
    return;
  if ((uint32_t)(now - lastPollAt_) < cfg_.pollIntervalMs)
    return;
  CommandType list[kMaxPoll];
  uint8_t n = buildPollList(list);
  if (pollIndex_ >= n)
    pollIndex_ = 0;
  Command c;
  if (!buildReadCommand(list[pollIndex_], CommandSource::Poll, now, c))
    return;
  pollIndex_ = (uint8_t)((pollIndex_ + 1) % n);
  lastPollAt_ = now;
  queue_.enqueue(c);
}

bool StoveController::transmit(Command &cmd, uint32_t now) {
  cmd.attempts++;
  cmd.sentAt = now;
  lastSendAt_ = now;
  EDK_LOGD(kTag, "-> %s %s id=%u reg=0x%04X val=0x%04X try=%u",
           commandTypeName(cmd.type), commandSourceName(cmd.source), cmd.id,
           cmd.reg, cmd.value, cmd.attempts);
  if (!link_.sendPdu(cmd.pdu)) {
    EDK_LOGW(kTag, "link refused write for %s", commandTypeName(cmd.type));
    return false;
  }
  return true;
}

void StoveController::sendNext(uint32_t now) {
  if (hasInflight_ || !linkReady_)
    return;
  if ((uint32_t)(now - lastSendAt_) < cfg_.interCommandGapMs)
    return;
  enqueuePollIfDue(now);
  Command c;
  if (!queue_.popNext(c))
    return;
  if (!transmit(c, now)) {
    if (c.isWrite()) {
      stats_.commandsFailed++;
      report(c, CommandState::Failed, "bluetooth write failed");
    }
    return;
  }
  inflight_ = c;
  hasInflight_ = true;
  report(inflight_, CommandState::Sent);
}

void StoveController::handleTimeout(uint32_t now) {
  Command c = inflight_;
  if (!c.isWrite() && c.attempts <= cfg_.readRetries) {
    EDK_LOGD(kTag, "no response for %s, retry", commandTypeName(c.type));
    if (transmit(inflight_, now))
      return;
  }
  hasInflight_ = false;
  stats_.requestsTimeout++;
  EDK_LOGW(kTag, "timeout for %s (id=%u)", commandTypeName(c.type), c.id);
  if (c.isWrite()) {
    // Never re-send a write blindly: verify what the stove actually did.
    report(c, CommandState::Timeout, "no echo from stove, verifying");
    scheduleVerify(c, now);
  } else if (c.source == CommandSource::Verify) {
    finishVerification(c, false, "verification read timed out");
  } else if (c.source == CommandSource::User) {
    stats_.commandsFailed++;
    report(c, CommandState::Failed, "no answer from stove");
  }
}

void StoveController::scheduleVerify(const Command &write, uint32_t now) {
  CommandType readType = verifyReadFor(write.type);
  if (readType == CommandType::None)
    return;
  Command v;
  if (!buildReadCommand(readType, CommandSource::Verify, now, v))
    return;
  v.verifiesId = write.id;
  v.verifiesType = write.type;
  v.expectedValue = write.value;
  memcpy(v.payload, write.payload, sizeof(v.payload));
  queue_.enqueue(v);
}

void StoveController::finishVerification(const Command &v, bool matched,
                                         const char *detail) {
  Command w;
  w.id = v.verifiesId;
  w.type = v.verifiesType;
  w.source = CommandSource::User;
  w.value = v.expectedValue;
  memcpy(w.payload, v.payload, sizeof(w.payload));
  if (matched) {
    stats_.commandsVerified++;
    report(w, CommandState::Verified, detail);
  } else {
    stats_.commandsFailed++;
    report(w, CommandState::Failed, detail);
  }
}

void StoveController::setPowerOnFromStatus(StoveStatus s, uint32_t now) {
  bool on;
  if (derivePowerOn(s, on))
    observe(state_.powerOn, on, Observable::PowerOn, now);
}

void StoveController::applyWriteValue(CommandType type, uint16_t value,
                                      uint32_t now) {
  switch (type) {
  case CommandType::SetOnOff:
    observe(state_.powerOn, (value & 0xFF) == 1, Observable::PowerOn, now);
    break;
  case CommandType::SetTargetTemp:
    if (value >= 50 && value <= 400)
      observe(state_.targetTempTenths, (int16_t)value, Observable::TargetTemp,
              now);
    break;
  case CommandType::SetFanPower: {
    FanPower fp = decodeFanPower(value);
    if (fp.valid) {
      observe(state_.fan1, fp.fan1, Observable::Fan1, now);
      observe(state_.power, fp.power, Observable::Power, now);
    }
    break;
  }
  case CommandType::SetAutomatic:
    observe(state_.automatic, decodeAutomatic(value), Observable::Automatic,
            now);
    break;
  case CommandType::SetRelax:
    observe(state_.relax, decodeRelax(value), Observable::Relax, now);
    break;
  case CommandType::SetStandby:
    observe(state_.standby, decodeStandby(value), Observable::Standby, now);
    break;
  case CommandType::SetChrono:
    observe(state_.chrono, decodeChrono(value), Observable::Chrono, now);
    break;
  default:
    break;
  }
}

void StoveController::handleWriteEcho(const ModbusResponse &r, uint32_t now) {
  Command c = inflight_;
  hasInflight_ = false;
  if (r.value != c.value) {
    EDK_LOGW(kTag, "%s echoed 0x%04X instead of 0x%04X",
             commandTypeName(c.type), r.value, c.value);
  }
  applyWriteValue(c.type, r.value, now);
  report(c, CommandState::Acknowledged,
         r.value == c.value ? "" : "stove echoed a different value");
  scheduleVerify(c, now);
}

void StoveController::handleReadResponse(const ModbusResponse &r,
                                         uint32_t now) {
  Command c = inflight_;
  hasInflight_ = false;
  uint16_t v = r.regValue(0);
  switch (c.type) {
  case CommandType::ReadMainStatus: {
    observe(state_.statusRaw, v, Observable::StatusRaw, now);
    StoveStatus s = decodeMainStatus(r.hi(0), r.lo(0));
    if (s == StoveStatus::Unknown) {
      EDK_LOGW(kTag, "unknown status combination %u/%u", r.hi(0), r.lo(0));
    } else {
      observe(state_.status, s, Observable::Status, now);
      setPowerOnFromStatus(s, now);
    }
    break;
  }
  case CommandType::ReadRoomTemp: {
    DecodedTemp t = decodeRoomTemp(r);
    if (t.valid)
      observe(state_.roomTempTenths, t.tenths, Observable::RoomTemp, now);
    else
      EDK_LOGD(kTag, "room temp not available (raw 0x%04X)", v);
    break;
  }
  case CommandType::ReadTargetTemp: {
    DecodedTemp t = decodeTargetTemp(r);
    if (t.valid)
      observe(state_.targetTempTenths, t.tenths, Observable::TargetTemp, now);
    else
      EDK_LOGW(kTag, "target temp implausible (raw 0x%04X)", v);
    break;
  }
  case CommandType::ReadFanPower: {
    FanPower fp = decodeFanPower(v);
    if (fp.valid) {
      observe(state_.fan1, fp.fan1, Observable::Fan1, now);
      observe(state_.power, fp.power, Observable::Power, now);
    } else {
      EDK_LOGD(kTag, "fan/power not available (raw 0x%04X)", v);
    }
    break;
  }
  case CommandType::ReadAutomatic:
    observe(state_.automatic, decodeAutomatic(v), Observable::Automatic, now);
    break;
  case CommandType::ReadThermocouple: {
    int16_t tc;
    if (decodeThermocoupleC(r, tc))
      observe(state_.thermocoupleC, tc, Observable::Thermocouple, now);
    break;
  }
  case CommandType::ReadRelax:
    observe(state_.relax, decodeRelax(v), Observable::Relax, now);
    break;
  case CommandType::ReadStandby:
    observe(state_.standby, decodeStandby(v), Observable::Standby, now);
    break;
  case CommandType::ReadChrono:
    observe(state_.chrono, decodeChrono(v), Observable::Chrono, now);
    break;
  case CommandType::ReadWarningFlags: {
    PelletLevel p = decodePelletLevel(r);
    if (p != PelletLevel::Unknown)
      observe(state_.pellet, p, Observable::Pellet, now);
    break;
  }
  case CommandType::ReadAlarm:
    observe(state_.alarmRaw, v, Observable::Alarm, now);
    break;
  case CommandType::ReadFan23:
    observe(state_.fan2, r.hi(0), Observable::Fan2, now);
    observe(state_.fan3, r.lo(0), Observable::Fan3, now);
    break;
  case CommandType::ReadPelletRemaining:
    observe(state_.pelletRemainingRaw, v, Observable::PelletRemaining, now);
    break;
  case CommandType::ReadControllerVersion:
    observe(state_.controllerVersionRaw, v, Observable::ControllerVersion, now);
    break;
  case CommandType::ReadRegister: {
    uint16_t vals[4];
    uint8_t n = (uint8_t)(r.byteCount / 2);
    if (n > 4)
      n = 4;
    for (uint8_t i = 0; i < n; i++)
      vals[i] = r.regValue(i);
    events_.onRegisterRead(c.reg, n, vals);
    char detail[64];
    int w = snprintf(detail, sizeof(detail), "0x%04X =", c.reg);
    for (uint8_t i = 0; i < n && w < (int)sizeof(detail) - 8; i++)
      w += snprintf(detail + w, sizeof(detail) - w, " 0x%04X", vals[i]);
    report(c, CommandState::Verified, detail);
    break;
  }
  default:
    break;
  }

  if (c.source == CommandSource::Verify) {
    char detail[64];
    bool ok;
    if (verifyModeFor(c.verifiesType) == VerifyMode::Observe) {
      bool on;
      bool wantOn = (c.expectedValue & 0xFF) == 1;
      if (derivePowerOn(state_.status.value, on) && state_.status.known) {
        ok = on == wantOn;
        snprintf(detail, sizeof(detail), "stove status: %s",
                 stoveStatusLabel(state_.status.value));
      } else {
        ok = true; // echo already confirmed; status not yet conclusive
        snprintf(detail, sizeof(detail), "acknowledged, status %s",
                 stoveStatusLabel(state_.status.value));
      }
    } else {
      ok = writeVerified(c.verifiesType, c.expectedValue, v);
      snprintf(detail, sizeof(detail), "read back 0x%04X, expected 0x%04X", v,
               c.expectedValue);
    }
    finishVerification(c, ok, detail);
  }
}

void StoveController::onFrame(const uint8_t plain[kBleBlockSize],
                              uint32_t now) {
  ModbusResponse r;
  FrameError err = parsePlainBlock(plain, r);
  if (err != FrameError::None) {
    stats_.framesInvalid++;
    EDK_LOGW(kTag, "invalid frame: %s", frameErrorName(err));
    return;
  }
  if (!hasInflight_) {
    stats_.framesUnexpected++;
    EDK_LOGD(kTag, "unsolicited frame fn=%u", r.function);
    return;
  }
  Command &c = inflight_;
  if (r.exception) {
    Command done = c;
    hasInflight_ = false;
    char reason[40];
    snprintf(reason, sizeof(reason), "modbus exception %u", r.exceptionCode);
    EDK_LOGW(kTag, "%s: %s", commandTypeName(done.type), reason);
    state_.markValidResponse(now);
    events_.onValidResponse(now);
    if (done.isWrite()) {
      stats_.commandsFailed++;
      report(done, CommandState::Failed, reason);
    } else if (done.source == CommandSource::Verify) {
      finishVerification(done, false, reason);
    } else if (done.source == CommandSource::User) {
      stats_.commandsFailed++;
      report(done, CommandState::Failed, reason);
    }
    return;
  }
  bool expectWrite = c.isWrite();
  if ((expectWrite && r.function != kFnWrite) ||
      (!expectWrite && r.function != kFnRead) ||
      (expectWrite && r.reg != c.reg) ||
      (!expectWrite && r.byteCount != c.value * 2)) {
    stats_.framesUnexpected++;
    EDK_LOGW(kTag, "frame does not match in-flight %s (fn=%u reg=0x%04X n=%u)",
             commandTypeName(c.type), r.function, r.reg, r.byteCount);
    return; // keep waiting for the real answer (or the timeout)
  }
  stats_.requestsOk++;
  state_.markValidResponse(now);
  events_.onValidResponse(now);
  if (expectWrite)
    handleWriteEcho(r, now);
  else
    handleReadResponse(r, now);
  updateValidity(now);
}

void StoveController::updateValidity(uint32_t now) {
  bool valid = state_.isFresh(now, cfg_.dataMaxAgeMs);
  if (valid != dataValid_) {
    dataValid_ = valid;
    EDK_LOGI(kTag, "stove data %s", valid ? "valid" : "stale");
    events_.onDataValidChanged(valid);
  }
}

void StoveController::tick(uint32_t now) {
  // Expire user/verify commands the stove could not be reached for.
  struct Ctx {
    StoveController *self;
    uint32_t now;
  } ctx{this, now};
  queue_.expire(
      now, cfg_.commandExpiryMs,
      [](const Command &c, void *p) {
        auto *x = static_cast<Ctx *>(p);
        if (c.source == CommandSource::Poll)
          return;
        Command dead = c;
        x->self->stats_.commandsExpired++;
        x->self->stats_.commandsFailed++;
        if (c.source == CommandSource::Verify)
          x->self->finishVerification(dead, false,
                                      "expired: stove unreachable");
        else
          x->self->report(dead, CommandState::Failed,
                          "expired: stove unreachable");
      },
      &ctx);

  updateValidity(now);
  if (!linkReady_)
    return;
  if (hasInflight_) {
    if ((uint32_t)(now - inflight_.sentAt) >= cfg_.responseTimeoutMs)
      handleTimeout(now);
    return;
  }
  sendNext(now);
}

} // namespace edk
