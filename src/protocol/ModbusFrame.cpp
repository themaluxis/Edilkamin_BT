#include "ModbusFrame.h"
#include <cstring>

namespace edk {

const uint8_t kFixedKey[16] = {0x31, 0xdd, 0x34, 0x51, 0x26, 0x39, 0x20, 0x23,
                               0x9f, 0x4b, 0x68, 0x20, 0xe7, 0x25, 0xfc, 0x75};

uint16_t crc16Modbus(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 0x0001)
        crc = (uint16_t)((crc >> 1) ^ 0xA001);
      else
        crc >>= 1;
    }
  }
  return crc; // low byte is transmitted first
}

void buildReadPdu(uint16_t reg, uint16_t count, uint8_t out[kPduSize]) {
  out[0] = kModbusSlave;
  out[1] = kFnRead;
  out[2] = (uint8_t)(reg >> 8);
  out[3] = (uint8_t)(reg & 0xFF);
  out[4] = (uint8_t)(count >> 8);
  out[5] = (uint8_t)(count & 0xFF);
}

void buildWritePdu(uint16_t reg, uint16_t value, uint8_t out[kPduSize]) {
  out[0] = kModbusSlave;
  out[1] = kFnWrite;
  out[2] = (uint8_t)(reg >> 8);
  out[3] = (uint8_t)(reg & 0xFF);
  out[4] = (uint8_t)(value >> 8);
  out[5] = (uint8_t)(value & 0xFF);
}

uint16_t pduRegister(const uint8_t pdu[kPduSize]) {
  return (uint16_t)((pdu[2] << 8) | pdu[3]);
}

uint16_t pduValueOrCount(const uint8_t pdu[kPduSize]) {
  return (uint16_t)((pdu[4] << 8) | pdu[5]);
}

bool pduIsWrite(const uint8_t pdu[kPduSize]) { return pdu[1] == kFnWrite; }

void buildPlainBlock(uint32_t unixTime, const uint8_t pdu[kPduSize],
                     uint8_t out[kBleBlockSize]) {
  out[0] = (uint8_t)(unixTime >> 24);
  out[1] = (uint8_t)(unixTime >> 16);
  out[2] = (uint8_t)(unixTime >> 8);
  out[3] = (uint8_t)(unixTime);
  memcpy(out + 4, kFixedKey, sizeof(kFixedKey));
  memcpy(out + kFrameHeaderSize, pdu, kPduSize);
  uint16_t crc = crc16Modbus(pdu, kPduSize);
  out[kFrameHeaderSize + 6] = (uint8_t)(crc & 0xFF);
  out[kFrameHeaderSize + 7] = (uint8_t)(crc >> 8);
  // PKCS#7 padding: 4 bytes of 0x04
  for (size_t i = kFrameHeaderSize + 8; i < kBleBlockSize; i++)
    out[i] = 0x04;
}

const char *frameErrorName(FrameError e) {
  switch (e) {
  case FrameError::None:
    return "ok";
  case FrameError::BadFixedKey:
    return "bad_fixed_key";
  case FrameError::BadPadding:
    return "bad_padding";
  case FrameError::BadCrc:
    return "bad_crc";
  case FrameError::BadSlave:
    return "bad_slave";
  case FrameError::UnsupportedFunction:
    return "unsupported_function";
  case FrameError::BadLength:
    return "bad_length";
  }
  return "?";
}

FrameError parsePlainBlock(const uint8_t block[kBleBlockSize],
                           ModbusResponse &out) {
  out = ModbusResponse();
  if (memcmp(block + 4, kFixedKey, sizeof(kFixedKey)) != 0)
    return FrameError::BadFixedKey;
  out.timestamp = ((uint32_t)block[0] << 24) | ((uint32_t)block[1] << 16) |
                  ((uint32_t)block[2] << 8) | block[3];

  const uint8_t *body = block + kFrameHeaderSize;
  uint8_t pad = body[kFrameBodyMax - 1];
  if (pad == 0 || pad > kFrameBodyMax - 5) // body must keep >= 5 bytes
    return FrameError::BadPadding;
  for (size_t i = kFrameBodyMax - pad; i < kFrameBodyMax; i++)
    if (body[i] != pad)
      return FrameError::BadPadding;
  size_t len = kFrameBodyMax - pad; // includes 2 CRC bytes

  uint16_t crc = crc16Modbus(body, len - 2);
  uint16_t rx = (uint16_t)(body[len - 2] | (body[len - 1] << 8));
  if (crc != rx)
    return FrameError::BadCrc;
  if (body[0] != kModbusSlave)
    return FrameError::BadSlave;

  uint8_t fn = body[1];
  if (fn & 0x80) {
    out.exception = true;
    out.function = fn & 0x7F;
    out.exceptionCode = body[2];
    return FrameError::None;
  }
  out.function = fn;
  if (fn == kFnRead) {
    out.byteCount = body[2];
    if (out.byteCount + 5u != len || out.byteCount > sizeof(out.data))
      return FrameError::BadLength;
    memcpy(out.data, body + 3, out.byteCount);
    out.value = out.regValue(0);
    return FrameError::None;
  }
  if (fn == kFnWrite) {
    if (len != 8)
      return FrameError::BadLength;
    out.reg = (uint16_t)((body[2] << 8) | body[3]);
    out.value = (uint16_t)((body[4] << 8) | body[5]);
    out.byteCount = 4;
    memcpy(out.data, body + 2, 4);
    return FrameError::None;
  }
  return FrameError::UnsupportedFunction;
}

} // namespace edk
