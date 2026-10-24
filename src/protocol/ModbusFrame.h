// Plain-text framing of the Edilkamin BLE protocol.
//
// Every BLE packet is a 32 byte AES-128-CBC block. Decrypted layout:
//   [0..3]   unix timestamp, big endian
//   [4..19]  fixed 16 byte key (constant, used here as an integrity check)
//   [20..31] modbus RTU frame (PDU + CRC16-modbus) followed by PKCS#7 padding
//
// Requests are always 6 byte PDUs (fn 3 read, fn 6 write single register),
// so request bodies are 8 bytes + 4 bytes of 0x04 padding.
// Responses vary: fn 3 → 3 + 2*count + 2 bytes, fn 6 → 8 bytes, exception → 5.
#pragma once
#include <cstddef>
#include <cstdint>

namespace edk {

constexpr size_t kBleBlockSize = 32;
constexpr size_t kFrameHeaderSize = 20;
constexpr size_t kFrameBodyMax = kBleBlockSize - kFrameHeaderSize; // 12
constexpr size_t kPduSize = 6;
constexpr uint8_t kModbusSlave = 0x01;
constexpr uint8_t kFnRead = 0x03;
constexpr uint8_t kFnWrite = 0x06;

extern const uint8_t kFixedKey[16];

uint16_t crc16Modbus(const uint8_t *data, size_t len);

void buildReadPdu(uint16_t reg, uint16_t count, uint8_t out[kPduSize]);
void buildWritePdu(uint16_t reg, uint16_t value, uint8_t out[kPduSize]);

// Extract register / value from a 6 byte PDU (either function).
uint16_t pduRegister(const uint8_t pdu[kPduSize]);
uint16_t pduValueOrCount(const uint8_t pdu[kPduSize]);
bool pduIsWrite(const uint8_t pdu[kPduSize]);

// Build the 32 byte plain block for a request (before AES).
void buildPlainBlock(uint32_t unixTime, const uint8_t pdu[kPduSize],
                     uint8_t out[kBleBlockSize]);

enum class FrameError : uint8_t {
  None = 0,
  BadFixedKey,
  BadPadding,
  BadCrc,
  BadSlave,
  UnsupportedFunction,
  BadLength,
};

const char *frameErrorName(FrameError e);

struct ModbusResponse {
  uint32_t timestamp = 0;
  uint8_t function = 0;   // 3 or 6 (exception bit stripped)
  bool exception = false; // fn | 0x80 received
  uint8_t exceptionCode = 0;
  // fn 3: byteCount and data[]; fn 6: reg + value (echo of the request)
  uint8_t byteCount = 0;
  uint8_t data[8] = {0};
  uint16_t reg = 0;
  uint16_t value = 0;

  // Register i of a read response (big endian), 0 when out of range.
  uint16_t regValue(size_t i) const {
    if (i * 2 + 1 >= byteCount)
      return 0;
    return (uint16_t)((data[i * 2] << 8) | data[i * 2 + 1]);
  }
  uint8_t hi(size_t i = 0) const { return (i * 2 < byteCount) ? data[i * 2] : 0; }
  uint8_t lo(size_t i = 0) const {
    return (i * 2 + 1 < byteCount) ? data[i * 2 + 1] : 0;
  }
};

// Parse a decrypted 32 byte block. Validates fixed key, PKCS#7 padding and
// CRC16 before accepting anything.
FrameError parsePlainBlock(const uint8_t block[kBleBlockSize],
                           ModbusResponse &out);

} // namespace edk
