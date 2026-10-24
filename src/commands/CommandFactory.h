// Turns MQTT (entity, payload) pairs into validated commands, and builds the
// periodic read commands. Pure: no I/O.
#pragma once
#include "Command.h"
#include "stove/StoveProfile.h"
#include "stove/StoveState.h"
#include <cstddef>

namespace edk {

// Strict numeric parsing: "17", "17.0", "17.5", "17.25" → tenths (rounded).
// Rejects anything else (empty, letters, comma, multiple dots, ...).
bool parseTemperatureTenths(const char *text, int16_t &tenths);

// Build the read command for a poll/verify type (count is type dependent).
bool buildReadCommand(CommandType type, CommandSource source, uint32_t now,
                      Command &out);

// Generic diagnostic read of 1..3 holding registers (function 3). Returns
// false for an invalid count.
bool buildRegisterRead(uint16_t reg, uint8_t count, uint32_t now, Command &out);

// Build a validated user command. Returns false with a reason on rejection.
// `entity` is the MQTT topic segment (e.g. "target_temperature").
bool buildUserCommand(const char *entity, const char *payload,
                      const StoveState &state, const StoveProfile &profile,
                      uint32_t now, Command &out, char *reason,
                      size_t reasonLen);

// Compare a read-back register value with what a write expected.
bool writeVerified(CommandType writeType, uint16_t expected,
                   uint16_t readValue);

} // namespace edk
