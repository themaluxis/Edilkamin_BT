// Helpers to build plain-text (decrypted) BLE blocks for tests.
#pragma once
#include "protocol/ModbusFrame.h"
#include <cstring>
#include <vector>

namespace testframes {

inline void makeBlock(uint32_t ts, const std::vector<uint8_t> &body,
                      uint8_t out[edk::kBleBlockSize]) {
  out[0] = (uint8_t)(ts >> 24);
  out[1] = (uint8_t)(ts >> 16);
  out[2] = (uint8_t)(ts >> 8);
  out[3] = (uint8_t)ts;
  memcpy(out + 4, edk::kFixedKey, 16);
  std::vector<uint8_t> b = body;
  uint16_t crc = edk::crc16Modbus(b.data(), b.size());
  b.push_back((uint8_t)(crc & 0xFF));
  b.push_back((uint8_t)(crc >> 8));
  uint8_t pad = (uint8_t)(edk::kFrameBodyMax - b.size());
  while (b.size() < edk::kFrameBodyMax)
    b.push_back(pad);
  memcpy(out + edk::kFrameHeaderSize, b.data(), edk::kFrameBodyMax);
}

// fn 3 response with N 16-bit registers
inline void readResponse(const std::vector<uint16_t> &regs,
                         uint8_t out[edk::kBleBlockSize], uint32_t ts = 1) {
  std::vector<uint8_t> body = {0x01, 0x03, (uint8_t)(regs.size() * 2)};
  for (uint16_t r : regs) {
    body.push_back((uint8_t)(r >> 8));
    body.push_back((uint8_t)(r & 0xFF));
  }
  makeBlock(ts, body, out);
}

// fn 6 echo
inline void writeEcho(uint16_t reg, uint16_t value,
                      uint8_t out[edk::kBleBlockSize], uint32_t ts = 1) {
  std::vector<uint8_t> body = {0x01,
                               0x06,
                               (uint8_t)(reg >> 8),
                               (uint8_t)(reg & 0xFF),
                               (uint8_t)(value >> 8),
                               (uint8_t)(value & 0xFF)};
  makeBlock(ts, body, out);
}

inline void exceptionResponse(uint8_t fn, uint8_t code,
                              uint8_t out[edk::kBleBlockSize]) {
  std::vector<uint8_t> body = {0x01, (uint8_t)(fn | 0x80), code};
  makeBlock(1, body, out);
}

} // namespace testframes
