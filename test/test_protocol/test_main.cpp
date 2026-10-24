#include "TestFrames.h"
#include "protocol/ModbusFrame.h"
#include <cstring>
#include <unity.h>

using namespace edk;

void setUp() {}
void tearDown() {}

// Vectors taken from the README captures (write_payload.png / response_payload.png)
static const uint8_t kWritePlain[32] = {
    0x64, 0xe0, 0x8f, 0xf4, 0x31, 0xdd, 0x34, 0x51, 0x26, 0x39, 0x20,
    0x23, 0x9f, 0x4b, 0x68, 0x20, 0xe7, 0x25, 0xfc, 0x75, 0x01, 0x03,
    0x02, 0xf4, 0x00, 0x02, 0x84, 0x41, 0x04, 0x04, 0x04, 0x04};
static const uint8_t kResponsePlain[32] = {
    0x20, 0xac, 0xe0, 0x64, 0x31, 0xdd, 0x34, 0x51, 0x26, 0x39, 0x20,
    0x23, 0x9f, 0x4b, 0x68, 0x20, 0xe7, 0x25, 0xfc, 0x75, 0x01, 0x03,
    0x04, 0x00, 0x00, 0x00, 0x60, 0xfa, 0x1b, 0x03, 0x03, 0x03};

void test_crc_matches_captures() {
  const uint8_t pdu[6] = {0x01, 0x03, 0x02, 0xf4, 0x00, 0x02};
  TEST_ASSERT_EQUAL_HEX16(0x4184, crc16Modbus(pdu, 6)); // tx 84 41
  const uint8_t resp[7] = {0x01, 0x03, 0x04, 0x00, 0x00, 0x00, 0x60};
  TEST_ASSERT_EQUAL_HEX16(0x1BFA, crc16Modbus(resp, 7)); // tx fa 1b
}

void test_build_plain_block_matches_capture() {
  uint8_t pdu[6];
  buildReadPdu(0x02F4, 2, pdu);
  uint8_t block[32];
  buildPlainBlock(0x64e08ff4, pdu, block);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(kWritePlain, block, 32);
}

void test_pdu_helpers() {
  uint8_t pdu[6];
  buildWritePdu(0x0525, 195, pdu);
  const uint8_t expect[6] = {0x01, 0x06, 0x05, 0x25, 0x00, 0xC3};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, pdu, 6);
  TEST_ASSERT_TRUE(pduIsWrite(pdu));
  TEST_ASSERT_EQUAL_HEX16(0x0525, pduRegister(pdu));
  TEST_ASSERT_EQUAL_UINT16(195, pduValueOrCount(pdu));
}

void test_parse_two_register_response() {
  ModbusResponse r;
  TEST_ASSERT_EQUAL(FrameError::None, parsePlainBlock(kResponsePlain, r));
  TEST_ASSERT_EQUAL_HEX32(0x20ace064, r.timestamp);
  TEST_ASSERT_EQUAL_UINT8(3, r.function);
  TEST_ASSERT_FALSE(r.exception);
  TEST_ASSERT_EQUAL_UINT8(4, r.byteCount);
  TEST_ASSERT_EQUAL_UINT16(0x0000, r.regValue(0));
  TEST_ASSERT_EQUAL_UINT16(0x0060, r.regValue(1));
  TEST_ASSERT_EQUAL_UINT16(0, r.regValue(2));
  TEST_ASSERT_EQUAL_UINT8(0x60, r.lo(1));
}

void test_parse_single_register_response() {
  uint8_t block[32];
  testframes::readResponse({204}, block, 0x11223344);
  ModbusResponse r;
  TEST_ASSERT_EQUAL(FrameError::None, parsePlainBlock(block, r));
  TEST_ASSERT_EQUAL_UINT8(2, r.byteCount);
  TEST_ASSERT_EQUAL_UINT16(204, r.regValue(0));
  TEST_ASSERT_EQUAL_UINT16(204, r.value);
  TEST_ASSERT_EQUAL_UINT8(0x05, block[31]); // 7 byte body → 5 bytes padding
}

void test_parse_write_echo() {
  uint8_t block[32];
  testframes::writeEcho(0x031C, 1, block);
  ModbusResponse r;
  TEST_ASSERT_EQUAL(FrameError::None, parsePlainBlock(block, r));
  TEST_ASSERT_EQUAL_UINT8(6, r.function);
  TEST_ASSERT_EQUAL_HEX16(0x031C, r.reg);
  TEST_ASSERT_EQUAL_UINT16(1, r.value);
}

void test_parse_exception() {
  uint8_t block[32];
  testframes::exceptionResponse(0x06, 0x02, block);
  ModbusResponse r;
  TEST_ASSERT_EQUAL(FrameError::None, parsePlainBlock(block, r));
  TEST_ASSERT_TRUE(r.exception);
  TEST_ASSERT_EQUAL_UINT8(6, r.function);
  TEST_ASSERT_EQUAL_UINT8(2, r.exceptionCode);
}

void test_parse_rejects_corruption() {
  uint8_t block[32];
  ModbusResponse r;

  memcpy(block, kResponsePlain, 32);
  block[23] ^= 0x01; // data byte → CRC mismatch
  TEST_ASSERT_EQUAL(FrameError::BadCrc, parsePlainBlock(block, r));

  memcpy(block, kResponsePlain, 32);
  block[31] = 0x02; // inconsistent padding
  TEST_ASSERT_EQUAL(FrameError::BadPadding, parsePlainBlock(block, r));

  memcpy(block, kResponsePlain, 32);
  block[5] ^= 0xFF; // fixed key (wrong AES key / garbage)
  TEST_ASSERT_EQUAL(FrameError::BadFixedKey, parsePlainBlock(block, r));

  memset(block, 0, 32);
  TEST_ASSERT_NOT_EQUAL(FrameError::None, parsePlainBlock(block, r));

  // byte count inconsistent with the frame length
  testframes::makeBlock(1, {0x01, 0x03, 0x04, 0x00, 0x01}, block);
  TEST_ASSERT_EQUAL(FrameError::BadLength, parsePlainBlock(block, r));

  // unsupported function
  testframes::makeBlock(1, {0x01, 0x10, 0x00, 0x00, 0x00, 0x01}, block);
  TEST_ASSERT_EQUAL(FrameError::UnsupportedFunction, parsePlainBlock(block, r));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_crc_matches_captures);
  RUN_TEST(test_build_plain_block_matches_capture);
  RUN_TEST(test_pdu_helpers);
  RUN_TEST(test_parse_two_register_response);
  RUN_TEST(test_parse_single_register_response);
  RUN_TEST(test_parse_write_echo);
  RUN_TEST(test_parse_exception);
  RUN_TEST(test_parse_rejects_corruption);
  return UNITY_END();
}
