// Pure decoders from modbus responses to typed values. Every decoder
// returns a validity flag so callers never publish implausible data.
#pragma once
#include "ModbusFrame.h"
#include "stove/StoveState.h"
#include <cstdint>

namespace edk {

struct DecodedTemp {
  bool valid = false;
  int16_t tenths = 0;
};

// Room temperature (reg MainEnvTemp). 0 means "remote in standby" → invalid.
// Values above 60.0 °C fall back to the low byte, which is what v0.x decoded;
// this keeps compatibility if the high byte turns out to carry other data.
DecodedTemp decodeRoomTemp(const ModbusResponse &r);
// Target temperature (reg TargetTemp), accepted range 5.0 .. 40.0 °C.
DecodedTemp decodeTargetTemp(const ModbusResponse &r);
// Thermocouple / exhaust temperature in whole °C (rounded), 0..600 °C.
bool decodeThermocoupleC(const ModbusResponse &r, int16_t &outC);

struct FanPower {
  bool valid = false;
  uint8_t fan1 = 0;  // 1..5, 6 = auto
  uint8_t power = 0; // 1..5
};
FanPower decodeFanPower(uint16_t regValue);

StoveStatus decodeMainStatus(uint8_t hi, uint8_t lo);
// Derive heating on/off from the status when unambiguous.
// Returns false when the status does not tell (keeps the previous value).
bool derivePowerOn(StoveStatus s, bool &outOn);

PelletLevel decodePelletLevel(const ModbusResponse &r);

// Single-bit style flags
bool decodeAutomatic(uint16_t regValue); // high byte
bool decodeRelax(uint16_t regValue);     // low byte
bool decodeStandby(uint16_t regValue);   // high byte
bool decodeChrono(uint16_t regValue);    // low byte

// Labels used on the MQTT state topics (kept identical to v0.x)
const char *fanModeLabel(uint8_t fan1);     // "20%".."100%", "Auto", or nullptr
const char *presetModeLabel(uint8_t power); // "Man. P1".."Man. P5" or nullptr
bool fanModeFromLabel(const char *label, uint8_t &fan1);
bool presetModeFromLabel(const char *label, uint8_t &power);

// Format tenths as "20.4" into buf. Returns buf.
const char *formatTenths(int16_t tenths, char *buf, size_t n);

} // namespace edk
