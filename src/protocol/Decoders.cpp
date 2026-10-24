#include "Decoders.h"
#include <cstdio>
#include <cstring>

namespace edk {

DecodedTemp decodeRoomTemp(const ModbusResponse &r) {
  DecodedTemp d;
  if (r.function != kFnRead || r.byteCount < 2)
    return d;
  uint16_t v = r.regValue(0);
  if (v == 0)
    return d; // remote control asleep, no reading
  if (v > 600) {
    // Legacy decoding used only the low byte. Keep it as a fallback.
    v = r.lo(0);
    if (v == 0)
      return d;
  }
  d.valid = true;
  d.tenths = (int16_t)v;
  return d;
}

DecodedTemp decodeTargetTemp(const ModbusResponse &r) {
  DecodedTemp d;
  uint16_t v;
  if (r.function == kFnRead && r.byteCount >= 2)
    v = r.regValue(0);
  else if (r.function == kFnWrite)
    v = r.value;
  else
    return d;
  if (v < 50 || v > 400)
    return d;
  d.valid = true;
  d.tenths = (int16_t)v;
  return d;
}

bool decodeThermocoupleC(const ModbusResponse &r, int16_t &outC) {
  if (r.function != kFnRead || r.byteCount < 2)
    return false;
  uint16_t v = r.regValue(0);
  if (v > 6000)
    return false;
  outC = (int16_t)((v + 5) / 10);
  return true;
}

FanPower decodeFanPower(uint16_t regValue) {
  FanPower fp;
  fp.fan1 = (uint8_t)(regValue >> 8);
  fp.power = (uint8_t)(regValue & 0xFF);
  fp.valid = fp.fan1 >= 1 && fp.fan1 <= 6 && fp.power >= 1 && fp.power <= 5;
  return fp;
}

StoveStatus decodeMainStatus(uint8_t hi, uint8_t lo) {
  if (lo == 1) {
    switch (hi) {
    case 1:
      return StoveStatus::Cleaning;
    case 3:
      return StoveStatus::LoadingPellets;
    case 4:
      return StoveStatus::IgnitionChargeSuspend;
    case 5:
      return StoveStatus::CheckExhaustTemp;
    case 6:
      return StoveStatus::ThresholdTempCheck;
    case 7:
      return StoveStatus::Warmup;
    default:
      return StoveStatus::Unknown;
    }
  }
  if (hi == 2 && lo == 2)
    return StoveStatus::Ok;
  if (hi == 0 && lo == 3)
    return StoveStatus::Shutdown;
  if (hi == 0 && lo == 4)
    return StoveStatus::Extinction;
  if (hi == 0 && lo == 0)
    return StoveStatus::Off;
  return StoveStatus::Unknown;
}

bool derivePowerOn(StoveStatus s, bool &outOn) {
  switch (s) {
  case StoveStatus::Off:
    outOn = false;
    return true;
  case StoveStatus::Cleaning:
  case StoveStatus::LoadingPellets:
  case StoveStatus::IgnitionChargeSuspend:
  case StoveStatus::CheckExhaustTemp:
  case StoveStatus::ThresholdTempCheck:
  case StoveStatus::Warmup:
  case StoveStatus::Ok:
    outOn = true;
    return true;
  case StoveStatus::Shutdown:
  case StoveStatus::Extinction:
  case StoveStatus::Unknown:
  default:
    return false; // ambiguous: cooling down or standby pause
  }
}

PelletLevel decodePelletLevel(const ModbusResponse &r) {
  if (r.function != kFnRead || r.byteCount < 2)
    return PelletLevel::Unknown;
  uint8_t flag = r.lo(0);
  if (flag == 0)
    return PelletLevel::Ok;
  if (flag == 4)
    return PelletLevel::Empty;
  return PelletLevel::Unknown;
}

bool decodeAutomatic(uint16_t v) { return ((v >> 8) & 0xFF) == 1; }
bool decodeRelax(uint16_t v) { return (v & 0xFF) == 1; }
bool decodeStandby(uint16_t v) { return ((v >> 8) & 0xFF) == 1; }
bool decodeChrono(uint16_t v) { return (v & 0xFF) == 1; }

static const char *const kFanLabels[] = {nullptr, "20%", "40%",  "60%",
                                         "80%",   "100%", "Auto"};
static const char *const kPresetLabels[] = {nullptr,   "Man. P1", "Man. P2",
                                            "Man. P3", "Man. P4", "Man. P5"};

const char *fanModeLabel(uint8_t fan1) {
  if (fan1 >= 1 && fan1 <= 6)
    return kFanLabels[fan1];
  return nullptr;
}

const char *presetModeLabel(uint8_t power) {
  if (power >= 1 && power <= 5)
    return kPresetLabels[power];
  return nullptr;
}

bool fanModeFromLabel(const char *label, uint8_t &fan1) {
  if (!label)
    return false;
  for (uint8_t i = 1; i <= 6; i++) {
    if (strcmp(label, kFanLabels[i]) == 0) {
      fan1 = i;
      return true;
    }
  }
  return false;
}

bool presetModeFromLabel(const char *label, uint8_t &power) {
  if (!label)
    return false;
  for (uint8_t i = 1; i <= 5; i++) {
    if (strcmp(label, kPresetLabels[i]) == 0) {
      power = i;
      return true;
    }
  }
  return false;
}

const char *formatTenths(int16_t tenths, char *buf, size_t n) {
  int16_t a = tenths < 0 ? (int16_t)-tenths : tenths;
  snprintf(buf, n, "%s%d.%d", tenths < 0 ? "-" : "", a / 10, a % 10);
  return buf;
}

} // namespace edk
