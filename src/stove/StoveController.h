// Orchestrates the command lifecycle (F02), the polling schedule and the
// confirmed stove state. Pure: talks to the BLE link and the outside world
// through two small interfaces so it can be unit tested natively.
#pragma once
#include "commands/CommandQueue.h"
#include "protocol/ModbusFrame.h"
#include "stove/StoveProfile.h"
#include "stove/StoveState.h"

namespace edk {

struct IStoveLink {
  virtual ~IStoveLink() = default;
  // Encrypt + send a 6 byte PDU. Returns false if the link is not writable.
  virtual bool sendPdu(const uint8_t pdu[kPduSize]) = 0;
};

struct IStoveEvents {
  virtual ~IStoveEvents() = default;
  // Lifecycle change of a user command (also final outcome of verifications).
  virtual void onCommandResult(const Command &cmd, const char *reason) {
    (void)cmd;
    (void)reason;
  }
  virtual void onObservedChanged(const StoveState &state, Observable what) {
    (void)state;
    (void)what;
  }
  virtual void onDataValidChanged(bool valid) { (void)valid; }
  virtual void onValidResponse(uint32_t now) { (void)now; }
  virtual void onError(const char *message) { (void)message; }
  virtual void onUserCommandQueued() {}
  // Result of a diagnostic register read requested with readRegister().
  virtual void onRegisterRead(uint16_t reg, uint8_t count,
                              const uint16_t *values) {
    (void)reg;
    (void)count;
    (void)values;
  }
};

class StoveController {
public:
  struct Config {
    uint32_t pollIntervalMs = 2000;
    uint32_t responseTimeoutMs = 2000;
    uint32_t commandExpiryMs = 120000;
    uint32_t dataMaxAgeMs = 360000;
    uint32_t interCommandGapMs = 100;
    uint8_t readRetries = 1; // extra attempts for reads only
    bool pollExperimental = false; // F09 registers (alarm, fans 2/3, ...)
    uint8_t fanCount = 1;          // >= 2 adds the fan 2/3 register
  };

  struct Stats {
    uint32_t requestsOk = 0;
    uint32_t requestsTimeout = 0;
    uint32_t framesInvalid = 0;
    uint32_t framesUnexpected = 0;
    uint32_t commandsRejected = 0;
    uint32_t commandsFailed = 0;
    uint32_t commandsVerified = 0;
    uint32_t commandsExpired = 0;
  };

  StoveController(IStoveLink &link, IStoveEvents &events,
                  const StoveProfile &profile = defaultProfile());

  void configure(const Config &cfg) { cfg_ = cfg; }
  const Config &config() const { return cfg_; }

  // Commands coming from MQTT: entity = topic segment, payload = text.
  void handleUserCommand(const char *entity, const char *payload, uint32_t now);
  // Diagnostic read of 1..3 registers (function 3 only, user priority).
  // Outcome: events.onRegisterRead + a command/result entry.
  void readRegister(uint16_t reg, uint8_t count, uint32_t now);
  // Commands are refused while BLE is administratively disabled.
  void setBleEnabled(bool enabled) { bleEnabled_ = enabled; }

  // Link lifecycle from the BLE manager.
  void setLinkReady(bool ready, uint32_t now);
  bool linkReady() const { return linkReady_; }
  // Decrypted 32 byte block received from the stove.
  void onFrame(const uint8_t plain[kBleBlockSize], uint32_t now);

  void tick(uint32_t now);

  bool hasPendingUserCommand() const {
    return (hasInflight_ && inflight_.source == CommandSource::User) ||
           queue_.hasPending(CommandSource::User) ||
           queue_.hasPending(CommandSource::Verify);
  }
  bool busy() const { return hasInflight_; }
  bool dataValid() const { return dataValid_; }

  const StoveState &state() const { return state_; }
  const CommandQueue &queue() const { return queue_; }
  const Stats &stats() const { return stats_; }
  void resetStats() { stats_ = Stats(); }
  bool inflight(Command &out) const {
    if (hasInflight_)
      out = inflight_;
    return hasInflight_;
  }

private:
  static constexpr uint8_t kPollCount = 10;
  static const CommandType kPollOrder[kPollCount];
  static constexpr uint8_t kMaxPoll = 16;
  uint8_t buildPollList(CommandType *out) const;

  void report(Command &cmd, CommandState st, const char *reason = "");
  void enqueueUser(Command cmd);
  void enqueuePollIfDue(uint32_t now);
  void sendNext(uint32_t now);
  bool transmit(Command &cmd, uint32_t now);
  void handleTimeout(uint32_t now);
  void handleReadResponse(const ModbusResponse &r, uint32_t now);
  void handleWriteEcho(const ModbusResponse &r, uint32_t now);
  void applyWriteValue(CommandType type, uint16_t value, uint32_t now);
  void scheduleVerify(const Command &write, uint32_t now);
  void finishVerification(const Command &verifyRead, bool matched,
                          const char *detail);
  void updateValidity(uint32_t now);
  void setPowerOnFromStatus(StoveStatus s, uint32_t now);

  template <typename T>
  void observe(Observed<T> &o, const T &v, Observable what, uint32_t now) {
    if (o.set(v, now))
      events_.onObservedChanged(state_, what);
  }

  IStoveLink &link_;
  IStoveEvents &events_;
  const StoveProfile &profile_;
  Config cfg_;
  CommandQueue queue_;
  StoveState state_;
  Stats stats_;
  Command inflight_;
  bool hasInflight_ = false;
  bool linkReady_ = false;
  bool bleEnabled_ = true;
  bool dataValid_ = false;
  uint32_t lastSendAt_ = 0;
  uint32_t lastPollAt_ = 0;
  uint8_t pollIndex_ = 0;
};

} // namespace edk
