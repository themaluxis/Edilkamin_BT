#include "CommandFactory.h"
#include "core/Strings.h"
#include "protocol/Decoders.h"
#include "protocol/Registers.h"
#include <cstdio>
#include <cstring>

namespace edk {

bool parseTemperatureTenths(const char *text, int16_t &tenths) {
  if (!text)
    return false;
  char buf[16];
  if (!copyStr(buf, sizeof(buf), text))
    return false;
  trimInPlace(buf);
  const char *p = buf;
  if (*p == '+')
    p++;
  int whole = 0, digits = 0;
  while (*p >= '0' && *p <= '9') {
    whole = whole * 10 + (*p - '0');
    p++;
    if (++digits > 3)
      return false;
  }
  if (digits == 0)
    return false;
  int frac = 0, fracDigits = 0;
  if (*p == '.') {
    p++;
    while (*p >= '0' && *p <= '9') {
      if (fracDigits < 2)
        frac = frac * 10 + (*p - '0');
      else if (*p != '0')
        return false; // more precision than the stove can take
      fracDigits++;
      p++;
    }
    if (fracDigits == 0)
      return false; // "17." is not a number we accept
  }
  if (*p != '\0')
    return false;
  int t = whole * 10;
  if (fracDigits == 1)
    t += frac;
  else if (fracDigits >= 2)
    t += (frac + 5) / 10; // round hundredths
  tenths = (int16_t)t;
  return true;
}

static void setPdu(Command &c, bool write, uint16_t reg, uint16_t valueOrCount) {
  c.reg = reg;
  c.value = valueOrCount;
  if (write)
    buildWritePdu(reg, valueOrCount, c.pdu);
  else
    buildReadPdu(reg, valueOrCount, c.pdu);
}

bool buildReadCommand(CommandType type, CommandSource source, uint32_t now,
                      Command &out) {
  out = Command();
  out.type = type;
  out.source = source;
  out.createdAt = now;
  out.state = CommandState::Validated;
  switch (type) {
  case CommandType::ReadMainStatus:
    setPdu(out, false, reg::FireplaceMainStatus, 1);
    return true;
  case CommandType::ReadRoomTemp:
    setPdu(out, false, reg::MainEnvTemp, 1);
    return true;
  case CommandType::ReadTargetTemp:
    setPdu(out, false, reg::TargetTemp, 1);
    return true;
  case CommandType::ReadFanPower:
    setPdu(out, false, reg::FanPower, 1);
    return true;
  case CommandType::ReadAutomatic:
    setPdu(out, false, reg::Automatic, 1);
    return true;
  case CommandType::ReadThermocouple:
    setPdu(out, false, reg::Thermocouple, 1);
    return true;
  case CommandType::ReadRelax:
    setPdu(out, false, reg::Relax, 1);
    return true;
  case CommandType::ReadStandby:
    setPdu(out, false, reg::Standby, 1);
    return true;
  case CommandType::ReadChrono:
    setPdu(out, false, reg::Chrono, 1);
    return true;
  case CommandType::ReadWarningFlags:
    setPdu(out, false, reg::WarningFlags, 2);
    return true;
  default:
    out = Command();
    return false;
  }
}

static bool reject(char *reason, size_t n, const char *msg) {
  if (reason && n)
    copyStr(reason, n, msg);
  return false;
}

static bool parseOnOff(const char *payload, bool &on) {
  if (strcmp(payload, "ON") == 0) {
    on = true;
    return true;
  }
  if (strcmp(payload, "OFF") == 0) {
    on = false;
    return true;
  }
  return false;
}

bool buildUserCommand(const char *entity, const char *payload,
                      const StoveState &state, const StoveProfile &profile,
                      uint32_t now, Command &out, char *reason,
                      size_t reasonLen) {
  out = Command();
  out.source = CommandSource::User;
  out.createdAt = now;
  if (reason && reasonLen)
    reason[0] = '\0';
  if (!entity || !payload)
    return reject(reason, reasonLen, "empty command");

  char pl[16];
  copyStr(pl, sizeof(pl), payload);
  trimInPlace(pl);
  copyStr(out.payload, sizeof(out.payload), pl);
  if (strlen(payload) >= sizeof(pl))
    return reject(reason, reasonLen, "payload too long");

  if (strcmp(entity, "hvac_mode") == 0) {
    out.type = CommandType::SetOnOff;
    if (strcmp(pl, "heat") == 0)
      setPdu(out, true, reg::OnOff, 1);
    else if (strcmp(pl, "off") == 0)
      setPdu(out, true, reg::OnOff, 0);
    else
      return reject(reason, reasonLen, "mode must be heat or off");
  } else if (strcmp(entity, "target_temperature") == 0) {
    out.type = CommandType::SetTargetTemp;
    int16_t tenths;
    if (!parseTemperatureTenths(pl, tenths))
      return reject(reason, reasonLen, "temperature is not a valid number");
    if (tenths < profile.minTargetTenths || tenths > profile.maxTargetTenths) {
      char msg[48];
      snprintf(msg, sizeof(msg), "temperature out of range %d.%d-%d.%d",
               profile.minTargetTenths / 10, profile.minTargetTenths % 10,
               profile.maxTargetTenths / 10, profile.maxTargetTenths % 10);
      return reject(reason, reasonLen, msg);
    }
    setPdu(out, true, reg::TargetTemp, (uint16_t)tenths);
  } else if (strcmp(entity, "fan_mode") == 0) {
    out.type = CommandType::SetFanPower;
    uint8_t fan;
    if (!fanModeFromLabel(pl, fan))
      return reject(reason, reasonLen, "unknown fan mode");
    if (fan == 6 && !profile.fanAutoSupported)
      return reject(reason, reasonLen, "fan auto not supported by profile");
    if (!state.power.known || state.power.value == 0)
      return reject(reason, reasonLen, "power level not yet known from stove");
    setPdu(out, true, reg::FanPower,
           (uint16_t)((fan << 8) | state.power.value));
  } else if (strcmp(entity, "preset_mode") == 0) {
    if (strcmp(pl, "Auto") == 0) {
      out.type = CommandType::SetAutomatic;
      setPdu(out, true, reg::Automatic, 0x0101);
    } else {
      out.type = CommandType::SetFanPower;
      uint8_t power;
      if (!presetModeFromLabel(pl, power))
        return reject(reason, reasonLen, "unknown preset");
      if (power > profile.powerLevels)
        return reject(reason, reasonLen, "power level not supported by profile");
      if (!state.fan1.known || state.fan1.value == 0)
        return reject(reason, reasonLen, "fan level not yet known from stove");
      setPdu(out, true, reg::FanPower,
             (uint16_t)((state.fan1.value << 8) | power));
    }
  } else if (strcmp(entity, "power") == 0) {
    // "P1".."P5" (select entity) or "1".."5"
    out.type = CommandType::SetFanPower;
    const char *q = pl;
    if (*q == 'P' || *q == 'p')
      q++;
    if (q[0] < '1' || q[0] > '9' || q[1] != '\0')
      return reject(reason, reasonLen, "power must be P1..P5");
    uint8_t power = (uint8_t)(q[0] - '0');
    if (power > profile.powerLevels)
      return reject(reason, reasonLen, "power level not supported by profile");
    if (!state.fan1.known || state.fan1.value == 0)
      return reject(reason, reasonLen, "fan level not yet known from stove");
    setPdu(out, true, reg::FanPower,
           (uint16_t)((state.fan1.value << 8) | power));
  } else if (strcmp(entity, "automatic_mode") == 0) {
    out.type = CommandType::SetAutomatic;
    bool on;
    if (!parseOnOff(pl, on))
      return reject(reason, reasonLen, "payload must be ON or OFF");
    setPdu(out, true, reg::Automatic, on ? 0x0101 : 0x0001);
  } else if (strcmp(entity, "relax") == 0) {
    out.type = CommandType::SetRelax;
    if (!profile.hasRelax)
      return reject(reason, reasonLen, "relax not supported by profile");
    bool on;
    if (!parseOnOff(pl, on))
      return reject(reason, reasonLen, "payload must be ON or OFF");
    setPdu(out, true, reg::Relax, on ? 0x0101 : 0x0000);
  } else if (strcmp(entity, "standby") == 0) {
    out.type = CommandType::SetStandby;
    if (!profile.hasStandby)
      return reject(reason, reasonLen, "standby not supported by profile");
    bool on;
    if (!parseOnOff(pl, on))
      return reject(reason, reasonLen, "payload must be ON or OFF");
    setPdu(out, true, reg::Standby, on ? 0x0101 : 0x0000);
  } else if (strcmp(entity, "chrono_mode") == 0) {
    out.type = CommandType::SetChrono;
    if (!profile.hasChrono)
      return reject(reason, reasonLen, "chrono not supported by profile");
    bool on;
    if (!parseOnOff(pl, on))
      return reject(reason, reasonLen, "payload must be ON or OFF");
    setPdu(out, true, reg::Chrono, on ? 0x0001 : 0x0000);
  } else {
    return reject(reason, reasonLen, "unknown entity");
  }
  out.state = CommandState::Validated;
  return true;
}

bool writeVerified(CommandType writeType, uint16_t expected,
                   uint16_t readValue) {
  switch (writeType) {
  case CommandType::SetTargetTemp:
  case CommandType::SetFanPower:
    return expected == readValue;
  case CommandType::SetAutomatic:
  case CommandType::SetStandby:
    return (expected >> 8) == (readValue >> 8);
  case CommandType::SetRelax:
  case CommandType::SetChrono:
    return (expected & 0xFF) == (readValue & 0xFF);
  default:
    return false;
  }
}

} // namespace edk
