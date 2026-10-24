#include "TestFrames.h"
#include "protocol/Registers.h"
#include "stove/StoveController.h"
#include <cstring>
#include <string>
#include <unity.h>
#include <vector>

using namespace edk;

struct FakeLink : IStoveLink {
  std::vector<std::vector<uint8_t>> sent;
  bool accept = true;
  bool sendPdu(const uint8_t pdu[kPduSize]) override {
    if (!accept)
      return false;
    sent.emplace_back(pdu, pdu + kPduSize);
    return true;
  }
  const std::vector<uint8_t> &last() const { return sent.back(); }
  uint16_t lastReg() const { return (uint16_t)((last()[2] << 8) | last()[3]); }
  bool lastIsWrite() const { return last()[1] == 0x06; }
};

struct Result {
  uint16_t id;
  CommandType type;
  CommandState state;
  std::string reason;
};

struct RegRead {
  uint16_t reg;
  std::vector<uint16_t> values;
};

struct FakeEvents : IStoveEvents {
  std::vector<RegRead> regReads;
  std::vector<Result> results;
  std::vector<Observable> changes;
  std::vector<bool> validity;
  std::vector<std::string> errors;
  int validResponses = 0;
  int wakeups = 0;
  void onCommandResult(const Command &c, const char *reason) override {
    results.push_back({c.id, c.type, c.state, reason ? reason : ""});
  }
  void onObservedChanged(const StoveState &, Observable what) override {
    changes.push_back(what);
  }
  void onDataValidChanged(bool v) override { validity.push_back(v); }
  void onValidResponse(uint32_t) override { validResponses++; }
  void onError(const char *m) override { errors.push_back(m); }
  void onUserCommandQueued() override { wakeups++; }
  void onRegisterRead(uint16_t reg, uint8_t count, const uint16_t *v) override {
    regReads.push_back({reg, std::vector<uint16_t>(v, v + count)});
  }
  bool sawChange(Observable o) const {
    for (auto c : changes)
      if (c == o)
        return true;
    return false;
  }
  const Result *lastResultFor(CommandType t) const {
    for (auto it = results.rbegin(); it != results.rend(); ++it)
      if (it->type == t)
        return &*it;
    return nullptr;
  }
  int countState(CommandState s) const {
    int n = 0;
    for (auto &r : results)
      if (r.state == s)
        n++;
    return n;
  }
};

struct Rig {
  FakeLink link;
  FakeEvents ev;
  StoveController ctl{link, ev};
  uint32_t now = 10000;
  Rig() {
    StoveController::Config c;
    c.pollIntervalMs = 2000;
    c.responseTimeoutMs = 2000;
    c.commandExpiryMs = 60000;
    c.dataMaxAgeMs = 30000;
    c.interCommandGapMs = 100;
    ctl.configure(c);
  }
  void advance(uint32_t ms) {
    now += ms;
    ctl.tick(now);
  }
  void ready() {
    ctl.setLinkReady(true, now);
    ctl.tick(now);
  }
  void respondRead(std::vector<uint16_t> regs) {
    uint8_t b[32];
    testframes::readResponse(regs, b);
    ctl.onFrame(b, now);
  }
  void respondEcho(uint16_t reg, uint16_t value) {
    uint8_t b[32];
    testframes::writeEcho(reg, value, b);
    ctl.onFrame(b, now);
  }
};

void setUp() {}
void tearDown() {}

void test_polling_starts_with_status_and_updates_state() {
  Rig r;
  TEST_ASSERT_EQUAL_UINT32(0, r.link.sent.size());
  r.ready();
  TEST_ASSERT_EQUAL_UINT32(1, r.link.sent.size());
  TEST_ASSERT_EQUAL_HEX16(reg::FireplaceMainStatus, r.link.lastReg());
  TEST_ASSERT_TRUE(r.ctl.busy());
  r.respondRead({0x0202}); // Ok
  TEST_ASSERT_FALSE(r.ctl.busy());
  TEST_ASSERT_EQUAL(StoveStatus::Ok, r.ctl.state().status.value);
  TEST_ASSERT_TRUE(r.ctl.state().powerOn.value);
  TEST_ASSERT_TRUE(r.ev.sawChange(Observable::Status));
  TEST_ASSERT_TRUE(r.ev.sawChange(Observable::PowerOn));
  TEST_ASSERT_EQUAL_INT(1, r.ev.validResponses);
  TEST_ASSERT_TRUE(r.ctl.dataValid());
  TEST_ASSERT_EQUAL_UINT32(1, r.ev.validity.size());
  TEST_ASSERT_TRUE(r.ev.validity[0]);
  // next poll waits for the poll interval
  r.advance(100);
  TEST_ASSERT_EQUAL_UINT32(1, r.link.sent.size());
  r.advance(1900);
  TEST_ASSERT_EQUAL_UINT32(2, r.link.sent.size());
  TEST_ASSERT_EQUAL_HEX16(reg::MainEnvTemp, r.link.lastReg());
  r.respondRead({204});
  TEST_ASSERT_EQUAL_INT16(204, r.ctl.state().roomTempTenths.value);
  // a second identical reading does not emit a change
  size_t changes = r.ev.changes.size();
  r.advance(2000);
  TEST_ASSERT_EQUAL_HEX16(reg::TargetTemp, r.link.lastReg());
  r.respondRead({170});
  TEST_ASSERT_EQUAL_UINT32(changes + 1, r.ev.changes.size());
  r.advance(2000);
  TEST_ASSERT_EQUAL_HEX16(reg::FanPower, r.link.lastReg());
  r.respondRead({0x0203});
  TEST_ASSERT_EQUAL_UINT8(2, r.ctl.state().fan1.value);
  TEST_ASSERT_EQUAL_UINT8(3, r.ctl.state().power.value);
  TEST_ASSERT_EQUAL_UINT32(4, r.ctl.stats().requestsOk);
}

void test_read_timeout_retries_once_then_gives_up() {
  Rig r;
  r.ready();
  TEST_ASSERT_EQUAL_UINT32(1, r.link.sent.size());
  r.advance(2000); // first timeout → retry same PDU
  TEST_ASSERT_EQUAL_UINT32(2, r.link.sent.size());
  TEST_ASSERT_TRUE(r.link.sent[0] == r.link.sent[1]);
  TEST_ASSERT_TRUE(r.ctl.busy());
  r.advance(2000); // second timeout → drop
  TEST_ASSERT_FALSE(r.ctl.busy());
  TEST_ASSERT_EQUAL_UINT32(1, r.ctl.stats().requestsTimeout);
  TEST_ASSERT_FALSE(r.ctl.dataValid());
  TEST_ASSERT_EQUAL_UINT32(0, r.ev.validity.size()); // never valid → no event
}

void test_user_write_lifecycle_acknowledged_then_verified() {
  Rig r;
  // command while the link is down: queued, BLE woken up, nothing sent
  r.ctl.handleUserCommand("target_temperature", "19.5", r.now);
  TEST_ASSERT_EQUAL_INT(1, r.ev.wakeups);
  TEST_ASSERT_TRUE(r.ctl.hasPendingUserCommand());
  TEST_ASSERT_EQUAL_UINT32(0, r.link.sent.size());
  TEST_ASSERT_EQUAL(CommandState::Queued, r.ev.results.back().state);
  uint16_t id = r.ev.results.back().id;
  TEST_ASSERT_NOT_EQUAL(0, id);

  r.ready(); // user command goes first, before any poll
  TEST_ASSERT_EQUAL_UINT32(1, r.link.sent.size());
  TEST_ASSERT_TRUE(r.link.lastIsWrite());
  TEST_ASSERT_EQUAL_HEX16(reg::TargetTemp, r.link.lastReg());
  TEST_ASSERT_EQUAL(CommandState::Sent, r.ev.results.back().state);

  r.respondEcho(reg::TargetTemp, 195);
  TEST_ASSERT_EQUAL(CommandState::Acknowledged, r.ev.results.back().state);
  TEST_ASSERT_EQUAL_UINT16(id, r.ev.results.back().id);
  TEST_ASSERT_EQUAL_INT16(195, r.ctl.state().targetTempTenths.value);
  TEST_ASSERT_TRUE(r.ev.sawChange(Observable::TargetTemp));

  // verification read follows immediately (after the inter-command gap)
  r.advance(100);
  TEST_ASSERT_EQUAL_UINT32(2, r.link.sent.size());
  TEST_ASSERT_FALSE(r.link.lastIsWrite());
  TEST_ASSERT_EQUAL_HEX16(reg::TargetTemp, r.link.lastReg());
  r.respondRead({195});
  const Result &last = r.ev.results.back();
  TEST_ASSERT_EQUAL(CommandState::Verified, last.state);
  TEST_ASSERT_EQUAL_UINT16(id, last.id);
  TEST_ASSERT_EQUAL(CommandType::SetTargetTemp, last.type);
  TEST_ASSERT_FALSE(r.ctl.hasPendingUserCommand());
  TEST_ASSERT_EQUAL_UINT32(1, r.ctl.stats().commandsVerified);
  TEST_ASSERT_EQUAL_UINT32(0, r.ev.errors.size());
}

void test_verification_mismatch_fails_and_corrects_state() {
  Rig r;
  r.ready();
  r.respondRead({0x0202});
  r.ctl.handleUserCommand("target_temperature", "22", r.now);
  r.advance(100);
  TEST_ASSERT_TRUE(r.link.lastIsWrite());
  r.respondEcho(reg::TargetTemp, 220);
  TEST_ASSERT_EQUAL_INT16(220, r.ctl.state().targetTempTenths.value);
  r.advance(100);
  r.respondRead({200}); // stove clamped it
  TEST_ASSERT_EQUAL(CommandState::Failed, r.ev.results.back().state);
  TEST_ASSERT_EQUAL_INT16(200, r.ctl.state().targetTempTenths.value);
  TEST_ASSERT_EQUAL_UINT32(1, r.ctl.stats().commandsFailed);
  TEST_ASSERT_EQUAL_UINT32(1, r.ev.errors.size());
}

void test_write_timeout_is_never_resent_but_verified() {
  Rig r;
  r.ready();
  r.respondRead({0x0000}); // Off
  r.ctl.handleUserCommand("hvac_mode", "heat", r.now);
  r.advance(100);
  TEST_ASSERT_TRUE(r.link.lastIsWrite());
  TEST_ASSERT_EQUAL_HEX16(reg::OnOff, r.link.lastReg());
  size_t sentBefore = r.link.sent.size();
  r.advance(2000); // timeout
  TEST_ASSERT_EQUAL(CommandState::Timeout, r.ev.results.back().state);
  // no blind re-send of the ignition command
  for (size_t i = sentBefore; i < r.link.sent.size(); i++)
    TEST_ASSERT_FALSE(r.link.sent[i][1] == 0x06);
  r.advance(100);
  TEST_ASSERT_FALSE(r.link.lastIsWrite());
  TEST_ASSERT_EQUAL_HEX16(reg::FireplaceMainStatus, r.link.lastReg());
  r.respondRead({0x0101}); // Cleaning → the stove did start
  TEST_ASSERT_EQUAL(CommandState::Verified, r.ev.results.back().state);
  TEST_ASSERT_EQUAL(CommandType::SetOnOff, r.ev.results.back().type);
  TEST_ASSERT_TRUE(r.ctl.state().powerOn.value);
}

void test_on_off_echo_sets_power_and_observes_status() {
  Rig r;
  r.ready();
  r.respondRead({0x0202});
  r.ctl.handleUserCommand("hvac_mode", "off", r.now);
  r.advance(100);
  r.respondEcho(reg::OnOff, 0);
  TEST_ASSERT_FALSE(r.ctl.state().powerOn.value);
  r.advance(100);
  TEST_ASSERT_EQUAL_HEX16(reg::FireplaceMainStatus, r.link.lastReg());
  r.respondRead({0x0004}); // Extinction: not conclusive → stays acknowledged
  TEST_ASSERT_EQUAL(CommandState::Verified, r.ev.results.back().state);
  TEST_ASSERT_NOT_NULL(strstr(r.ev.results.back().reason.c_str(), "Extinction"));
}

void test_invalid_frames_are_counted_and_ignored() {
  Rig r;
  r.ready();
  uint8_t b[32];
  testframes::readResponse({0x0202}, b);
  b[25] ^= 0xFF; // corrupt
  r.ctl.onFrame(b, r.now);
  TEST_ASSERT_EQUAL_UINT32(1, r.ctl.stats().framesInvalid);
  TEST_ASSERT_TRUE(r.ctl.busy()); // still waiting
  // a response that does not match the in-flight read is ignored too
  r.respondEcho(reg::OnOff, 1);
  TEST_ASSERT_EQUAL_UINT32(1, r.ctl.stats().framesUnexpected);
  TEST_ASSERT_TRUE(r.ctl.busy());
  r.respondRead({0x0202});
  TEST_ASSERT_FALSE(r.ctl.busy());
  // unsolicited frame when idle
  r.respondRead({0x0202});
  TEST_ASSERT_EQUAL_UINT32(2, r.ctl.stats().framesUnexpected);
}

void test_rejections() {
  Rig r;
  r.ctl.handleUserCommand("target_temperature", "abc", r.now);
  TEST_ASSERT_EQUAL(CommandState::Rejected, r.ev.results.back().state);
  TEST_ASSERT_EQUAL_UINT32(1, r.ctl.stats().commandsRejected);
  TEST_ASSERT_EQUAL_UINT32(1, r.ev.errors.size());
  TEST_ASSERT_EQUAL_INT(0, r.ev.wakeups);
  r.ctl.setBleEnabled(false);
  r.ctl.handleUserCommand("relax", "ON", r.now);
  TEST_ASSERT_EQUAL(CommandState::Rejected, r.ev.results.back().state);
  TEST_ASSERT_EQUAL_STRING("bluetooth disabled", r.ev.results.back().reason.c_str());
  TEST_ASSERT_FALSE(r.ctl.hasPendingUserCommand());
}

void test_superseded_commands_are_reported() {
  Rig r;
  r.ctl.handleUserCommand("target_temperature", "18", r.now);
  uint16_t first = r.ev.results.back().id;
  r.ctl.handleUserCommand("target_temperature", "19", r.now);
  TEST_ASSERT_EQUAL_INT(2, r.ev.countState(CommandState::Queued));
  TEST_ASSERT_EQUAL_INT(1, r.ev.countState(CommandState::Rejected));
  const Result *rej = nullptr;
  for (auto &x : r.ev.results)
    if (x.state == CommandState::Rejected)
      rej = &x;
  TEST_ASSERT_NOT_NULL(rej);
  TEST_ASSERT_EQUAL_UINT16(first, rej->id);
  TEST_ASSERT_EQUAL_UINT32(1, r.ctl.queue().size());
  r.ready();
  TEST_ASSERT_EQUAL_UINT8(0, r.link.last()[4]);
  TEST_ASSERT_EQUAL_UINT8(190, r.link.last()[5]);
}

void test_commands_expire_when_stove_unreachable() {
  Rig r;
  r.ctl.handleUserCommand("relax", "ON", r.now);
  r.advance(59000);
  TEST_ASSERT_TRUE(r.ctl.hasPendingUserCommand());
  r.advance(2000);
  TEST_ASSERT_FALSE(r.ctl.hasPendingUserCommand());
  TEST_ASSERT_EQUAL(CommandState::Failed, r.ev.results.back().state);
  TEST_ASSERT_NOT_NULL(strstr(r.ev.results.back().reason.c_str(), "expired"));
  TEST_ASSERT_EQUAL_UINT32(1, r.ctl.stats().commandsExpired);
}

void test_link_loss_settles_inflight_and_flushes_polls() {
  Rig r;
  r.ready();
  r.respondRead({0x0202});
  r.ctl.handleUserCommand("relax", "ON", r.now);
  r.advance(100);
  TEST_ASSERT_TRUE(r.link.lastIsWrite());
  r.ctl.setLinkReady(false, r.now);
  TEST_ASSERT_FALSE(r.ctl.busy());
  TEST_ASSERT_EQUAL(CommandState::Timeout, r.ev.results.back().state);
  // a verification read is waiting for the next session
  TEST_ASSERT_TRUE(r.ctl.queue().hasPending(CommandSource::Verify));
  TEST_ASSERT_FALSE(r.ctl.queue().hasPending(CommandSource::Poll));
  size_t sent = r.link.sent.size();
  r.advance(5000);
  TEST_ASSERT_EQUAL_UINT32(sent, r.link.sent.size()); // link down: nothing sent
  r.ctl.setLinkReady(true, r.now);
  r.ctl.tick(r.now);
  TEST_ASSERT_EQUAL_HEX16(reg::Relax, r.link.lastReg());
  r.respondRead({0x0101});
  TEST_ASSERT_EQUAL(CommandState::Verified, r.ev.results.back().state);
  TEST_ASSERT_TRUE(r.ctl.state().relax.value);
}

void test_data_goes_stale() {
  Rig r;
  r.ready();
  r.respondRead({0x0202});
  TEST_ASSERT_TRUE(r.ctl.dataValid());
  r.ctl.setLinkReady(false, r.now);
  r.advance(29000);
  TEST_ASSERT_TRUE(r.ctl.dataValid());
  r.advance(2000);
  TEST_ASSERT_FALSE(r.ctl.dataValid());
  TEST_ASSERT_EQUAL_UINT32(2, r.ev.validity.size());
  TEST_ASSERT_FALSE(r.ev.validity[1]);
}

void test_modbus_exception_fails_write() {
  Rig r;
  r.ready();
  r.respondRead({0x0202});
  r.ctl.handleUserCommand("standby", "ON", r.now);
  r.advance(100);
  uint8_t b[32];
  testframes::exceptionResponse(0x06, 0x03, b);
  r.ctl.onFrame(b, r.now);
  TEST_ASSERT_EQUAL(CommandState::Failed, r.ev.results.back().state);
  TEST_ASSERT_NOT_NULL(strstr(r.ev.results.back().reason.c_str(), "exception 3"));
  TEST_ASSERT_FALSE(r.ctl.busy());
}

void test_automatic_and_pellet_paths() {
  Rig r;
  r.ready();
  r.respondRead({0x0202});
  // move the poll ring to Automatic (index 4) by answering polls
  r.advance(2000);
  r.respondRead({204});
  r.advance(2000);
  r.respondRead({170});
  r.advance(2000);
  r.respondRead({0x0203});
  r.advance(2000);
  TEST_ASSERT_EQUAL_HEX16(reg::Automatic, r.link.lastReg());
  r.respondRead({0x0100});
  TEST_ASSERT_TRUE(r.ctl.state().automatic.value);
  r.advance(2000);
  TEST_ASSERT_EQUAL_HEX16(reg::Thermocouple, r.link.lastReg());
  r.respondRead({1234});
  TEST_ASSERT_EQUAL_INT16(123, r.ctl.state().thermocoupleC.value);
  r.advance(2000);
  r.respondRead({0x0001}); // relax
  r.advance(2000);
  r.respondRead({0x0100}); // standby
  r.advance(2000);
  r.respondRead({0x0000}); // chrono
  r.advance(2000);
  TEST_ASSERT_EQUAL_HEX16(reg::WarningFlags, r.link.lastReg());
  TEST_ASSERT_EQUAL_UINT8(2, r.link.last()[5]); // 2 registers
  r.respondRead({0x0004, 0x0060});
  TEST_ASSERT_EQUAL(PelletLevel::Empty, r.ctl.state().pellet.value);
  TEST_ASSERT_TRUE(r.ctl.state().relax.value);
  TEST_ASSERT_TRUE(r.ctl.state().standby.value);
  TEST_ASSERT_FALSE(r.ctl.state().chrono.value);
  // ring wraps around
  r.advance(2000);
  TEST_ASSERT_EQUAL_HEX16(reg::FireplaceMainStatus, r.link.lastReg());
}

void test_status_raw_is_always_observed() {
  Rig r;
  r.ready();
  r.respondRead({0x0905}); // unknown combination
  TEST_ASSERT_TRUE(r.ev.sawChange(Observable::StatusRaw));
  TEST_ASSERT_EQUAL_HEX16(0x0905, r.ctl.state().statusRaw.value);
  TEST_ASSERT_FALSE(r.ctl.state().status.known); // label not invented
}

void test_register_read_tool() {
  Rig r;
  r.ready();
  r.respondRead({0x0202});
  r.ctl.readRegister(0x02E6, 2, r.now);
  r.advance(100);
  TEST_ASSERT_EQUAL_HEX16(0x02E6, r.link.lastReg());
  TEST_ASSERT_FALSE(r.link.lastIsWrite());
  TEST_ASSERT_EQUAL_UINT8(2, r.link.last()[5]); // count
  r.respondRead({0x1234, 0x0001});
  TEST_ASSERT_EQUAL_UINT32(1, r.ev.regReads.size());
  TEST_ASSERT_EQUAL_HEX16(0x02E6, r.ev.regReads[0].reg);
  TEST_ASSERT_EQUAL_UINT32(2, r.ev.regReads[0].values.size());
  TEST_ASSERT_EQUAL_HEX16(0x1234, r.ev.regReads[0].values[0]);
  const Result *res = r.ev.lastResultFor(CommandType::ReadRegister);
  TEST_ASSERT_NOT_NULL(res);
  TEST_ASSERT_EQUAL(CommandState::Verified, res->state);
  TEST_ASSERT_NOT_NULL(strstr(res->reason.c_str(), "0x1234"));
  // invalid count rejected, nothing sent
  size_t sent = r.link.sent.size();
  r.ctl.readRegister(0x02E6, 4, r.now);
  r.advance(100);
  TEST_ASSERT_EQUAL_UINT32(sent, r.link.sent.size());
  TEST_ASSERT_EQUAL(CommandState::Rejected, r.ev.lastResultFor(CommandType::ReadRegister)->state);
  // no answer: retried once, then FAILED with a result
  r.ctl.readRegister(0x0032, 1, r.now);
  r.advance(100);
  r.advance(2000);
  r.advance(2000);
  TEST_ASSERT_EQUAL(CommandState::Failed, r.ev.lastResultFor(CommandType::ReadRegister)->state);
  TEST_ASSERT_FALSE(r.ctl.busy());
}

void test_experimental_polls_only_when_enabled() {
  Rig r;
  r.ready();
  std::vector<uint16_t> regs;
  for (int i = 0; i < 12; i++) {
    regs.push_back(r.link.lastReg());
    if (r.link.last()[5] == 2)
      r.respondRead({0x0202, 0x0000});
    else
      r.respondRead({0x0202});
    r.advance(2000);
  }
  bool sawAlarm = false;
  for (uint16_t x : regs)
    if (x == reg::Alarm)
      sawAlarm = true;
  TEST_ASSERT_FALSE(sawAlarm);

  Rig e;
  StoveController::Config c = e.ctl.config();
  c.pollExperimental = true;
  c.fanCount = 3;
  e.ctl.configure(c);
  e.ready();
  bool alarm = false, fan23 = false, version = false, pellets = false;
  for (int i = 0; i < 16; i++) {
    uint16_t x = e.link.lastReg();
    alarm |= x == reg::Alarm;
    fan23 |= x == reg::Fan23;
    version |= x == reg::MotherboardAppVersion;
    pellets |= x == reg::PelletRemaining;
    if (x == reg::Fan23)
      e.respondRead({0x0203});
    else if (e.link.last()[5] == 2)
      e.respondRead({0x0042, 0x0000});
    else
      e.respondRead({0x0042});
    e.advance(2000);
  }
  TEST_ASSERT_TRUE(alarm && fan23 && version && pellets);
  TEST_ASSERT_TRUE(e.ev.sawChange(Observable::Alarm));
  TEST_ASSERT_EQUAL_UINT8(2, e.ctl.state().fan2.value);
  TEST_ASSERT_EQUAL_UINT8(3, e.ctl.state().fan3.value);
  TEST_ASSERT_TRUE(e.ctl.state().controllerVersionRaw.known);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_polling_starts_with_status_and_updates_state);
  RUN_TEST(test_read_timeout_retries_once_then_gives_up);
  RUN_TEST(test_user_write_lifecycle_acknowledged_then_verified);
  RUN_TEST(test_verification_mismatch_fails_and_corrects_state);
  RUN_TEST(test_write_timeout_is_never_resent_but_verified);
  RUN_TEST(test_on_off_echo_sets_power_and_observes_status);
  RUN_TEST(test_invalid_frames_are_counted_and_ignored);
  RUN_TEST(test_rejections);
  RUN_TEST(test_superseded_commands_are_reported);
  RUN_TEST(test_commands_expire_when_stove_unreachable);
  RUN_TEST(test_link_loss_settles_inflight_and_flushes_polls);
  RUN_TEST(test_data_goes_stale);
  RUN_TEST(test_modbus_exception_fails_write);
  RUN_TEST(test_automatic_and_pellet_paths);
  RUN_TEST(test_status_raw_is_always_observed);
  RUN_TEST(test_register_read_tool);
  RUN_TEST(test_experimental_polls_only_when_enabled);
  return UNITY_END();
}
