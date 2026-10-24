#include "TestFrames.h"
#include "protocol/Decoders.h"
#include <cstring>
#include <unity.h>

using namespace edk;

void setUp() {}
void tearDown() {}

static ModbusResponse readOf(uint16_t v) {
  uint8_t block[32];
  testframes::readResponse({v}, block);
  ModbusResponse r;
  parsePlainBlock(block, r);
  return r;
}

void test_room_temperature() {
  DecodedTemp t = decodeRoomTemp(readOf(204));
  TEST_ASSERT_TRUE(t.valid);
  TEST_ASSERT_EQUAL_INT16(204, t.tenths);
  TEST_ASSERT_FALSE(decodeRoomTemp(readOf(0)).valid); // remote asleep
  t = decodeRoomTemp(readOf(265));                     // 26.5 °C
  TEST_ASSERT_TRUE(t.valid);
  TEST_ASSERT_EQUAL_INT16(265, t.tenths);
  t = decodeRoomTemp(readOf(0x1ACC)); // implausible → legacy low byte
  TEST_ASSERT_TRUE(t.valid);
  TEST_ASSERT_EQUAL_INT16(204, t.tenths);
  // write echoes are not room temperatures
  uint8_t block[32];
  testframes::writeEcho(0x02E9, 204, block);
  ModbusResponse w;
  parsePlainBlock(block, w);
  TEST_ASSERT_FALSE(decodeRoomTemp(w).valid);
}

void test_target_temperature() {
  TEST_ASSERT_TRUE(decodeTargetTemp(readOf(170)).valid);
  TEST_ASSERT_EQUAL_INT16(170, decodeTargetTemp(readOf(170)).tenths);
  TEST_ASSERT_FALSE(decodeTargetTemp(readOf(0)).valid);
  TEST_ASSERT_FALSE(decodeTargetTemp(readOf(900)).valid);
  uint8_t block[32];
  testframes::writeEcho(0x0525, 195, block);
  ModbusResponse w;
  parsePlainBlock(block, w);
  TEST_ASSERT_TRUE(decodeTargetTemp(w).valid);
  TEST_ASSERT_EQUAL_INT16(195, decodeTargetTemp(w).tenths);
}

void test_thermocouple_rounding() {
  int16_t c = 0;
  TEST_ASSERT_TRUE(decodeThermocoupleC(readOf(1234), c));
  TEST_ASSERT_EQUAL_INT16(123, c);
  TEST_ASSERT_TRUE(decodeThermocoupleC(readOf(1235), c));
  TEST_ASSERT_EQUAL_INT16(124, c);
  TEST_ASSERT_FALSE(decodeThermocoupleC(readOf(9000), c));
}

void test_fan_power() {
  FanPower fp = decodeFanPower(0x0603);
  TEST_ASSERT_TRUE(fp.valid);
  TEST_ASSERT_EQUAL_UINT8(6, fp.fan1);
  TEST_ASSERT_EQUAL_UINT8(3, fp.power);
  TEST_ASSERT_FALSE(decodeFanPower(0x0000).valid);
  TEST_ASSERT_FALSE(decodeFanPower(0x0706).valid);
}

void test_status_table() {
  TEST_ASSERT_EQUAL(StoveStatus::Cleaning, decodeMainStatus(1, 1));
  TEST_ASSERT_EQUAL(StoveStatus::LoadingPellets, decodeMainStatus(3, 1));
  TEST_ASSERT_EQUAL(StoveStatus::IgnitionChargeSuspend, decodeMainStatus(4, 1));
  TEST_ASSERT_EQUAL(StoveStatus::CheckExhaustTemp, decodeMainStatus(5, 1));
  TEST_ASSERT_EQUAL(StoveStatus::ThresholdTempCheck, decodeMainStatus(6, 1));
  TEST_ASSERT_EQUAL(StoveStatus::Warmup, decodeMainStatus(7, 1));
  TEST_ASSERT_EQUAL(StoveStatus::Ok, decodeMainStatus(2, 2));
  TEST_ASSERT_EQUAL(StoveStatus::Shutdown, decodeMainStatus(0, 3));
  TEST_ASSERT_EQUAL(StoveStatus::Extinction, decodeMainStatus(0, 4));
  TEST_ASSERT_EQUAL(StoveStatus::Off, decodeMainStatus(0, 0));
  TEST_ASSERT_EQUAL(StoveStatus::Unknown, decodeMainStatus(9, 9));
  TEST_ASSERT_EQUAL_STRING("Loading Pellets",
                           stoveStatusLabel(StoveStatus::LoadingPellets));

  bool on = false;
  TEST_ASSERT_TRUE(derivePowerOn(StoveStatus::Ok, on));
  TEST_ASSERT_TRUE(on);
  TEST_ASSERT_TRUE(derivePowerOn(StoveStatus::Off, on));
  TEST_ASSERT_FALSE(on);
  TEST_ASSERT_FALSE(derivePowerOn(StoveStatus::Extinction, on));
  TEST_ASSERT_FALSE(derivePowerOn(StoveStatus::Unknown, on));
}

void test_pellet_and_flags() {
  uint8_t block[32];
  testframes::readResponse({0x0000, 0x0060}, block);
  ModbusResponse r;
  parsePlainBlock(block, r);
  TEST_ASSERT_EQUAL(PelletLevel::Ok, decodePelletLevel(r));
  testframes::readResponse({0x0004, 0x0060}, block);
  parsePlainBlock(block, r);
  TEST_ASSERT_EQUAL(PelletLevel::Empty, decodePelletLevel(r));
  testframes::readResponse({0x0002, 0x0060}, block);
  parsePlainBlock(block, r);
  TEST_ASSERT_EQUAL(PelletLevel::Unknown, decodePelletLevel(r));

  TEST_ASSERT_TRUE(decodeAutomatic(0x0101));
  TEST_ASSERT_FALSE(decodeAutomatic(0x0001));
  TEST_ASSERT_TRUE(decodeRelax(0x0101));
  TEST_ASSERT_FALSE(decodeRelax(0x0100));
  TEST_ASSERT_TRUE(decodeStandby(0x0100));
  TEST_ASSERT_TRUE(decodeChrono(0x0001));
}

void test_labels() {
  TEST_ASSERT_EQUAL_STRING("60%", fanModeLabel(3));
  TEST_ASSERT_EQUAL_STRING("Auto", fanModeLabel(6));
  TEST_ASSERT_NULL(fanModeLabel(0));
  TEST_ASSERT_NULL(fanModeLabel(7));
  TEST_ASSERT_EQUAL_STRING("Man. P5", presetModeLabel(5));
  TEST_ASSERT_NULL(presetModeLabel(6));
  uint8_t v = 0;
  TEST_ASSERT_TRUE(fanModeFromLabel("100%", v));
  TEST_ASSERT_EQUAL_UINT8(5, v);
  TEST_ASSERT_TRUE(presetModeFromLabel("Man. P2", v));
  TEST_ASSERT_EQUAL_UINT8(2, v);
  TEST_ASSERT_FALSE(presetModeFromLabel("P2", v));
  char buf[12];
  TEST_ASSERT_EQUAL_STRING("20.4", formatTenths(204, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("17.0", formatTenths(170, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("-0.5", formatTenths(-5, buf, sizeof(buf)));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_room_temperature);
  RUN_TEST(test_target_temperature);
  RUN_TEST(test_thermocouple_rounding);
  RUN_TEST(test_fan_power);
  RUN_TEST(test_status_table);
  RUN_TEST(test_pellet_and_flags);
  RUN_TEST(test_labels);
  return UNITY_END();
}
