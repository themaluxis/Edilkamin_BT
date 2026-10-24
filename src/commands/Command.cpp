#include "Command.h"

namespace edk {

const char *commandTypeName(CommandType t) {
  switch (t) {
  case CommandType::None:
    return "none";
  case CommandType::ReadMainStatus:
    return "READ_MAIN_STATUS";
  case CommandType::ReadRoomTemp:
    return "READ_ROOM_TEMP";
  case CommandType::ReadTargetTemp:
    return "READ_TARGET_TEMP";
  case CommandType::ReadFanPower:
    return "READ_FAN_POWER";
  case CommandType::ReadAutomatic:
    return "READ_AUTOMATIC";
  case CommandType::ReadThermocouple:
    return "READ_THERMOCOUPLE";
  case CommandType::ReadRelax:
    return "READ_RELAX";
  case CommandType::ReadStandby:
    return "READ_STANDBY";
  case CommandType::ReadChrono:
    return "READ_CHRONO";
  case CommandType::ReadWarningFlags:
    return "READ_WARNING_FLAGS";
  case CommandType::SetOnOff:
    return "SET_ON_OFF";
  case CommandType::SetTargetTemp:
    return "SET_TARGET_TEMP";
  case CommandType::SetFanPower:
    return "SET_FAN_POWER";
  case CommandType::SetAutomatic:
    return "SET_AUTOMATIC";
  case CommandType::SetRelax:
    return "SET_RELAX";
  case CommandType::SetStandby:
    return "SET_STANDBY";
  case CommandType::SetChrono:
    return "SET_CHRONO";
  default:
    return "?";
  }
}

const char *commandStateName(CommandState s) {
  switch (s) {
  case CommandState::Received:
    return "RECEIVED";
  case CommandState::Validated:
    return "VALIDATED";
  case CommandState::Queued:
    return "QUEUED";
  case CommandState::Sent:
    return "SENT";
  case CommandState::Acknowledged:
    return "ACKNOWLEDGED";
  case CommandState::Verified:
    return "VERIFIED";
  case CommandState::Rejected:
    return "REJECTED";
  case CommandState::Timeout:
    return "TIMEOUT";
  case CommandState::Failed:
    return "FAILED";
  }
  return "?";
}

const char *commandSourceName(CommandSource s) {
  switch (s) {
  case CommandSource::Poll:
    return "poll";
  case CommandSource::Verify:
    return "verify";
  case CommandSource::User:
    return "user";
  }
  return "?";
}

bool commandIsWrite(CommandType t) {
  return t >= CommandType::SetOnOff && t < CommandType::Count;
}

bool commandIsTerminal(CommandState s) {
  return s == CommandState::Verified || s == CommandState::Rejected ||
         s == CommandState::Timeout || s == CommandState::Failed;
}

const char *commandEntity(CommandType t) {
  switch (t) {
  case CommandType::SetOnOff:
  case CommandType::ReadMainStatus:
    return "hvac_mode";
  case CommandType::SetTargetTemp:
  case CommandType::ReadTargetTemp:
    return "target_temperature";
  case CommandType::SetFanPower:
  case CommandType::ReadFanPower:
    return "fan_power";
  case CommandType::SetAutomatic:
  case CommandType::ReadAutomatic:
    return "automatic_mode";
  case CommandType::SetRelax:
  case CommandType::ReadRelax:
    return "relax";
  case CommandType::SetStandby:
  case CommandType::ReadStandby:
    return "standby";
  case CommandType::SetChrono:
  case CommandType::ReadChrono:
    return "chrono_mode";
  case CommandType::ReadRoomTemp:
    return "temperature";
  case CommandType::ReadThermocouple:
    return "thermocouple_temperature";
  case CommandType::ReadWarningFlags:
    return "pellet_level";
  default:
    return "none";
  }
}

CommandType verifyReadFor(CommandType write) {
  switch (write) {
  case CommandType::SetOnOff:
    return CommandType::ReadMainStatus;
  case CommandType::SetTargetTemp:
    return CommandType::ReadTargetTemp;
  case CommandType::SetFanPower:
    return CommandType::ReadFanPower;
  case CommandType::SetAutomatic:
    return CommandType::ReadAutomatic;
  case CommandType::SetRelax:
    return CommandType::ReadRelax;
  case CommandType::SetStandby:
    return CommandType::ReadStandby;
  case CommandType::SetChrono:
    return CommandType::ReadChrono;
  default:
    return CommandType::None;
  }
}

VerifyMode verifyModeFor(CommandType write) {
  switch (write) {
  case CommandType::SetOnOff:
    return VerifyMode::Observe;
  case CommandType::SetTargetTemp:
  case CommandType::SetFanPower:
  case CommandType::SetAutomatic:
  case CommandType::SetRelax:
  case CommandType::SetStandby:
  case CommandType::SetChrono:
    return VerifyMode::Strict;
  default:
    return VerifyMode::None;
  }
}

} // namespace edk
