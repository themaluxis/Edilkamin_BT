#include "commands/CommandFactory.h"
#include "commands/CommandQueue.h"
#include "protocol/Registers.h"
#include <cstring>
#include <unity.h>

using namespace edk;

void setUp() {}
void tearDown() {}

void test_temperature_parsing() {
  int16_t t;
  TEST_ASSERT_TRUE(parseTemperatureTenths("17", t));
  TEST_ASSERT_EQUAL_INT16(170, t);
  TEST_ASSERT_TRUE(parseTemperatureTenths("17.0", t));
  TEST_ASSERT_EQUAL_INT16(170, t);
  TEST_ASSERT_TRUE(parseTemperatureTenths("17.5", t));
  TEST_ASSERT_EQUAL_INT16(175, t);
  TEST_ASSERT_TRUE(parseTemperatureTenths("17.25", t));
  TEST_ASSERT_EQUAL_INT16(173, t);
  TEST_ASSERT_TRUE(parseTemperatureTenths("17.50", t));
  TEST_ASSERT_EQUAL_INT16(175, t);
  TEST_ASSERT_TRUE(parseTemperatureTenths("17.500", t));
  TEST_ASSERT_EQUAL_INT16(175, t);
  TEST_ASSERT_TRUE(parseTemperatureTenths(" 19 ", t));
  TEST_ASSERT_EQUAL_INT16(190, t);
  TEST_ASSERT_TRUE(parseTemperatureTenths("+21.5", t));
  TEST_ASSERT_EQUAL_INT16(215, t);
  TEST_ASSERT_FALSE(parseTemperatureTenths("17.", t));
  TEST_ASSERT_FALSE(parseTemperatureTenths("17,5", t));
  TEST_ASSERT_FALSE(parseTemperatureTenths("abc", t));
  TEST_ASSERT_FALSE(parseTemperatureTenths("", t));
  TEST_ASSERT_FALSE(parseTemperatureTenths(nullptr, t));
  TEST_ASSERT_FALSE(parseTemperatureTenths("1e3", t));
  TEST_ASSERT_FALSE(parseTemperatureTenths("17.555", t));
  TEST_ASSERT_FALSE(parseTemperatureTenths("1234", t));
  TEST_ASSERT_FALSE(parseTemperatureTenths("-5", t));
  TEST_ASSERT_FALSE(parseTemperatureTenths("17.5x", t));
}

void test_user_commands() {
  StoveState st;
  Command c;
  char reason[64];
  const StoveProfile &p = defaultProfile();

  TEST_ASSERT_TRUE(buildUserCommand("hvac_mode", "heat", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_EQUAL(CommandType::SetOnOff, c.type);
  const uint8_t on[6] = {0x01, 0x06, 0x03, 0x1C, 0x00, 0x01};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(on, c.pdu, 6);
  TEST_ASSERT_EQUAL(CommandState::Validated, c.state);
  TEST_ASSERT_EQUAL(CommandSource::User, c.source);
  TEST_ASSERT_EQUAL_STRING("heat", c.payload);

  TEST_ASSERT_FALSE(buildUserCommand("hvac_mode", "cool", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_EQUAL_STRING("mode must be heat or off", reason);

  TEST_ASSERT_TRUE(buildUserCommand("target_temperature", "19.5", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_EQUAL(CommandType::SetTargetTemp, c.type);
  TEST_ASSERT_EQUAL_HEX16(reg::TargetTemp, c.reg);
  TEST_ASSERT_EQUAL_UINT16(195, c.value);
  TEST_ASSERT_EQUAL_UINT8(0x00, c.pdu[4]);
  TEST_ASSERT_EQUAL_UINT8(0xC3, c.pdu[5]);

  TEST_ASSERT_TRUE(buildUserCommand("target_temperature", "17", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_EQUAL_UINT16(170, c.value); // not 17 → 1.7 °C like v0.x

  TEST_ASSERT_FALSE(buildUserCommand("target_temperature", "31", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_NOT_NULL(strstr(reason, "out of range"));
  TEST_ASSERT_FALSE(buildUserCommand("target_temperature", "14.5", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_FALSE(buildUserCommand("target_temperature", "nineteen", st, p, 1, c, reason, sizeof(reason)));

  // fan/power share one register: refuse until the other half is known
  TEST_ASSERT_FALSE(buildUserCommand("fan_mode", "60%", st, p, 1, c, reason, sizeof(reason)));
  st.power.set((uint8_t)3, 1);
  st.fan1.set((uint8_t)2, 1);
  TEST_ASSERT_TRUE(buildUserCommand("fan_mode", "60%", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_EQUAL(CommandType::SetFanPower, c.type);
  TEST_ASSERT_EQUAL_HEX16(0x0303, c.value);
  TEST_ASSERT_TRUE(buildUserCommand("fan_mode", "Auto", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_EQUAL_HEX16(0x0603, c.value);
  TEST_ASSERT_FALSE(buildUserCommand("fan_mode", "70%", st, p, 1, c, reason, sizeof(reason)));

  TEST_ASSERT_TRUE(buildUserCommand("preset_mode", "Man. P5", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_EQUAL(CommandType::SetFanPower, c.type);
  TEST_ASSERT_EQUAL_HEX16(0x0205, c.value);
  TEST_ASSERT_TRUE(buildUserCommand("preset_mode", "Auto", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_EQUAL(CommandType::SetAutomatic, c.type);
  TEST_ASSERT_EQUAL_HEX16(0x0101, c.value);

  // power select entity: P1..P5 or bare digit, keeps the current fan level
  TEST_ASSERT_TRUE(buildUserCommand("power", "P4", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_EQUAL(CommandType::SetFanPower, c.type);
  TEST_ASSERT_EQUAL_HEX16(0x0204, c.value);
  TEST_ASSERT_TRUE(buildUserCommand("power", "1", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_EQUAL_HEX16(0x0201, c.value);
  TEST_ASSERT_FALSE(buildUserCommand("power", "P6", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_FALSE(buildUserCommand("power", "P0", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_FALSE(buildUserCommand("power", "P12", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_FALSE(buildUserCommand("power", "Auto", st, p, 1, c, reason, sizeof(reason)));
  {
    StoveState unknownFan;
    TEST_ASSERT_FALSE(buildUserCommand("power", "P2", unknownFan, p, 1, c, reason, sizeof(reason)));
    TEST_ASSERT_NOT_NULL(strstr(reason, "fan level"));
  }

  TEST_ASSERT_TRUE(buildUserCommand("automatic_mode", "OFF", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_EQUAL_HEX16(0x0001, c.value);
  TEST_ASSERT_TRUE(buildUserCommand("relax", "ON", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_EQUAL_HEX16(0x0101, c.value);
  TEST_ASSERT_TRUE(buildUserCommand("standby", "OFF", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_EQUAL_HEX16(0x0000, c.value);
  TEST_ASSERT_TRUE(buildUserCommand("chrono_mode", "ON", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_EQUAL_HEX16(0x0001, c.value);
  TEST_ASSERT_FALSE(buildUserCommand("relax", "on", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_FALSE(buildUserCommand("airkare", "ON", st, p, 1, c, reason, sizeof(reason)));
  TEST_ASSERT_EQUAL_STRING("unknown entity", reason);
  TEST_ASSERT_FALSE(buildUserCommand("relax", "ONNNNNNNNNNNNNNNNNNNNNNNNNN", st, p, 1, c, reason, sizeof(reason)));
}

void test_read_commands() {
  Command c;
  TEST_ASSERT_TRUE(buildReadCommand(CommandType::ReadWarningFlags, CommandSource::Poll, 5, c));
  const uint8_t expect[6] = {0x01, 0x03, 0x02, 0xF4, 0x00, 0x02};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, c.pdu, 6);
  TEST_ASSERT_EQUAL_UINT16(2, c.value);
  TEST_ASSERT_FALSE(c.isWrite());
  TEST_ASSERT_TRUE(buildReadCommand(CommandType::ReadFanPower, CommandSource::Verify, 5, c));
  TEST_ASSERT_EQUAL_HEX16(reg::FanPower, c.reg);
  TEST_ASSERT_FALSE(buildReadCommand(CommandType::SetOnOff, CommandSource::Poll, 5, c));
}

void test_verify_comparison() {
  TEST_ASSERT_TRUE(writeVerified(CommandType::SetTargetTemp, 195, 195));
  TEST_ASSERT_FALSE(writeVerified(CommandType::SetTargetTemp, 195, 190));
  TEST_ASSERT_TRUE(writeVerified(CommandType::SetAutomatic, 0x0101, 0x0100));
  TEST_ASSERT_FALSE(writeVerified(CommandType::SetAutomatic, 0x0101, 0x0001));
  TEST_ASSERT_TRUE(writeVerified(CommandType::SetRelax, 0x0101, 0x0001));
  TEST_ASSERT_TRUE(writeVerified(CommandType::SetChrono, 0x0000, 0x0100));
  TEST_ASSERT_FALSE(writeVerified(CommandType::SetOnOff, 1, 1)); // observe mode
  TEST_ASSERT_EQUAL(VerifyMode::Observe, verifyModeFor(CommandType::SetOnOff));
  TEST_ASSERT_EQUAL(CommandType::ReadTargetTemp, verifyReadFor(CommandType::SetTargetTemp));
}

static Command mk(CommandType t, CommandSource s, uint16_t reg, uint16_t v, uint32_t now) {
  Command c;
  c.type = t;
  c.source = s;
  c.reg = reg;
  c.value = v;
  c.createdAt = now;
  if (commandIsWrite(t))
    buildWritePdu(reg, v, c.pdu);
  else
    buildReadPdu(reg, v, c.pdu);
  return c;
}

void test_queue_priority_and_fifo() {
  CommandQueue q;
  TEST_ASSERT_EQUAL(CommandQueue::EnqueueResult::Added,
                    q.enqueue(mk(CommandType::ReadRoomTemp, CommandSource::Poll, reg::MainEnvTemp, 1, 0)));
  TEST_ASSERT_EQUAL(CommandQueue::EnqueueResult::Added,
                    q.enqueue(mk(CommandType::ReadFanPower, CommandSource::Verify, reg::FanPower, 1, 0)));
  TEST_ASSERT_EQUAL(CommandQueue::EnqueueResult::Added,
                    q.enqueue(mk(CommandType::SetRelax, CommandSource::User, reg::Relax, 0x0101, 0)));
  TEST_ASSERT_EQUAL(CommandQueue::EnqueueResult::Added,
                    q.enqueue(mk(CommandType::SetStandby, CommandSource::User, reg::Standby, 0, 0)));
  TEST_ASSERT_EQUAL_UINT32(4, q.size());
  Command c;
  TEST_ASSERT_TRUE(q.popNext(c));
  TEST_ASSERT_EQUAL(CommandType::SetRelax, c.type); // user first, FIFO
  TEST_ASSERT_EQUAL(CommandState::Queued, c.state);
  TEST_ASSERT_NOT_EQUAL(0, c.id);
  TEST_ASSERT_TRUE(q.popNext(c));
  TEST_ASSERT_EQUAL(CommandType::SetStandby, c.type);
  TEST_ASSERT_TRUE(q.popNext(c));
  TEST_ASSERT_EQUAL(CommandType::ReadFanPower, c.type); // verify before poll
  TEST_ASSERT_TRUE(q.popNext(c));
  TEST_ASSERT_EQUAL(CommandType::ReadRoomTemp, c.type);
  TEST_ASSERT_FALSE(q.popNext(c));
}

void test_queue_dedup_rules() {
  CommandQueue q;
  Command superseded;
  Command t1 = mk(CommandType::SetTargetTemp, CommandSource::User, reg::TargetTemp, 170, 0);
  Command t2 = mk(CommandType::SetTargetTemp, CommandSource::User, reg::TargetTemp, 200, 1);
  TEST_ASSERT_EQUAL(CommandQueue::EnqueueResult::Added, q.enqueue(t1));
  TEST_ASSERT_EQUAL(CommandQueue::EnqueueResult::Replaced, q.enqueue(t2, &superseded));
  TEST_ASSERT_EQUAL_UINT16(170, superseded.value);
  TEST_ASSERT_EQUAL_UINT32(1, q.size());
  Command c;
  q.popNext(c);
  TEST_ASSERT_EQUAL_UINT16(200, c.value); // latest wins

  // identical poll reads collapse, verify reads upgrade a pending poll
  TEST_ASSERT_EQUAL(CommandQueue::EnqueueResult::Added,
                    q.enqueue(mk(CommandType::ReadRelax, CommandSource::Poll, reg::Relax, 1, 0)));
  TEST_ASSERT_EQUAL(CommandQueue::EnqueueResult::Duplicate,
                    q.enqueue(mk(CommandType::ReadRelax, CommandSource::Poll, reg::Relax, 1, 0)));
  TEST_ASSERT_EQUAL_UINT32(1, q.size());
  TEST_ASSERT_EQUAL(CommandQueue::EnqueueResult::Added,
                    q.enqueue(mk(CommandType::ReadRelax, CommandSource::Verify, reg::Relax, 1, 0)));
  TEST_ASSERT_EQUAL_UINT32(1, q.size());
  TEST_ASSERT_TRUE(q.hasPending(CommandSource::Verify));
  TEST_ASSERT_FALSE(q.hasPending(CommandSource::Poll));
  TEST_ASSERT_TRUE(q.containsRead(reg::Relax));
}

void test_queue_overflow_and_eviction() {
  CommandQueue q;
  for (size_t i = 0; i < CommandQueue::kCapacity; i++)
    TEST_ASSERT_EQUAL(CommandQueue::EnqueueResult::Added,
                      q.enqueue(mk(CommandType::ReadRoomTemp, CommandSource::Poll, (uint16_t)(0x100 + i), 1, i)));
  TEST_ASSERT_EQUAL_UINT32(CommandQueue::kCapacity, q.size());
  // one more poll: dropped and counted
  TEST_ASSERT_EQUAL(CommandQueue::EnqueueResult::Full,
                    q.enqueue(mk(CommandType::ReadRoomTemp, CommandSource::Poll, 0x200, 1, 99)));
  TEST_ASSERT_EQUAL_UINT32(1, q.overflowCount());
  // a user command evicts the oldest poll instead of being lost
  TEST_ASSERT_EQUAL(CommandQueue::EnqueueResult::Added,
                    q.enqueue(mk(CommandType::SetRelax, CommandSource::User, reg::Relax, 0x0101, 100)));
  TEST_ASSERT_EQUAL_UINT32(CommandQueue::kCapacity, q.size());
  TEST_ASSERT_FALSE(q.containsRead(0x100));
  TEST_ASSERT_TRUE(q.containsRead(0x101));
  Command c;
  q.popNext(c);
  TEST_ASSERT_EQUAL(CommandType::SetRelax, c.type);
  // a queue full of user commands rejects further ones
  CommandQueue q2;
  for (size_t i = 0; i < CommandQueue::kCapacity; i++)
    q2.enqueue(mk(CommandType::SetTargetTemp, CommandSource::User, (uint16_t)(0x300 + i), 1, i));
  TEST_ASSERT_EQUAL(CommandQueue::EnqueueResult::Full,
                    q2.enqueue(mk(CommandType::SetRelax, CommandSource::User, reg::Relax, 1, 5)));
}

static int g_visited = 0;
static void countVisit(const Command &, void *) { g_visited++; }

void test_queue_expiry_and_flush() {
  CommandQueue q;
  q.enqueue(mk(CommandType::SetRelax, CommandSource::User, reg::Relax, 1, 1000));
  q.enqueue(mk(CommandType::ReadRoomTemp, CommandSource::Poll, reg::MainEnvTemp, 1, 1000));
  q.enqueue(mk(CommandType::SetStandby, CommandSource::User, reg::Standby, 1, 50000));
  g_visited = 0;
  TEST_ASSERT_EQUAL_UINT32(2, q.expire(61500, 60000, countVisit, nullptr));
  TEST_ASSERT_EQUAL_INT(2, g_visited);
  TEST_ASSERT_EQUAL_UINT32(1, q.size());
  q.enqueue(mk(CommandType::ReadRoomTemp, CommandSource::Poll, reg::MainEnvTemp, 1, 70000));
  TEST_ASSERT_EQUAL_UINT32(1, q.removeBySource(CommandSource::Poll));
  TEST_ASSERT_EQUAL_UINT32(1, q.size());
  q.clear();
  TEST_ASSERT_FALSE(q.hasPending());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_temperature_parsing);
  RUN_TEST(test_user_commands);
  RUN_TEST(test_read_commands);
  RUN_TEST(test_verify_comparison);
  RUN_TEST(test_queue_priority_and_fifo);
  RUN_TEST(test_queue_dedup_rules);
  RUN_TEST(test_queue_overflow_and_eviction);
  RUN_TEST(test_queue_expiry_and_flush);
  return UNITY_END();
}
