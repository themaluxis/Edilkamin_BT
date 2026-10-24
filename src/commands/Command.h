// Command model: every request to the stove carries an observable lifecycle
//   RECEIVED → VALIDATED → QUEUED → SENT → ACKNOWLEDGED → VERIFIED
// with REJECTED / TIMEOUT / FAILED as terminal failure states.
#pragma once
#include "protocol/ModbusFrame.h"
#include <cstdint>

namespace edk {

enum class CommandType : uint8_t {
  None = 0,
  // reads (safe, repeatable)
  ReadMainStatus,
  ReadRoomTemp,
  ReadTargetTemp,
  ReadFanPower,
  ReadAutomatic,
  ReadThermocouple,
  ReadRelax,
  ReadStandby,
  ReadChrono,
  ReadWarningFlags,
  // writes (sensitive, never auto-retried)
  SetOnOff,
  SetTargetTemp,
  SetFanPower, // both fan1 and power share one register
  SetAutomatic,
  SetRelax,
  SetStandby,
  SetChrono,
  Count
};

enum class CommandSource : uint8_t { Poll = 0, Verify, User };

enum class CommandState : uint8_t {
  Received = 0,
  Validated,
  Queued,
  Sent,
  Acknowledged,
  Verified,
  Rejected,
  Timeout,
  Failed,
};

// How a write is confirmed after the stove echoed it.
enum class VerifyMode : uint8_t {
  None = 0, // reads
  Strict,   // read back the register and compare
  Observe,  // re-read status, success already decided by the echo (on/off)
};

const char *commandTypeName(CommandType t);
const char *commandStateName(CommandState s);
const char *commandSourceName(CommandSource s);
bool commandIsWrite(CommandType t);
bool commandIsTerminal(CommandState s);
// Entity (MQTT topic segment) that a command type reports on.
const char *commandEntity(CommandType t);
// The read command that verifies a given write.
CommandType verifyReadFor(CommandType write);
VerifyMode verifyModeFor(CommandType write);

struct Command {
  uint16_t id = 0;
  CommandType type = CommandType::None;
  CommandSource source = CommandSource::Poll;
  CommandState state = CommandState::Received;
  uint8_t pdu[kPduSize] = {0};
  uint16_t reg = 0;
  uint16_t value = 0; // written value or read count
  uint8_t attempts = 0;
  uint32_t createdAt = 0;
  uint32_t sentAt = 0;
  // verification link (set on Verify reads)
  uint16_t verifiesId = 0;
  CommandType verifiesType = CommandType::None;
  uint16_t expectedValue = 0;
  // human readable payload that triggered the command (for result reports)
  char payload[12] = {0};

  bool isWrite() const { return commandIsWrite(type); }
  bool empty() const { return type == CommandType::None; }
};

} // namespace edk
