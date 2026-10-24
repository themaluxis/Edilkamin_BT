// Runtime-configurable logger with an in-memory ring buffer (F03).
// Pure C++: sinks (Serial, Telnet, MQTT) are attached by the platform layer.
#pragma once
#include "core/Log.h"
#include <cstddef>
#include <cstdint>

namespace edk {

class Logger {
public:
  static constexpr size_t kLineLen = 120;
  static constexpr size_t kLines = 48;
  static constexpr size_t kMaxSinks = 3;

  using Sink = void (*)(LogLevel level, const char *line, void *ctx);
  using UptimeFn = uint32_t (*)();
  // Writes "HH:MM:SS" (8 chars + NUL) when wall clock is known; else false.
  using WallClockFn = bool (*)(char *out, size_t n);
  using DumpFn = void (*)(const char *line, void *ctx);

  void begin(UptimeFn uptime, WallClockFn wall = nullptr);

  // Level changes take effect immediately. revertAfterMs > 0 makes the change
  // temporary (e.g. a TRACE session) and restores the previous level later.
  void setLevel(LogLevel level, uint32_t revertAfterMs = 0);
  LogLevel level() const { return level_; }
  LogLevel baseLevel() const { return baseLevel_; }
  bool temporary() const { return revertAt_ != 0; }

  bool addSink(Sink sink, void *ctx, LogLevel maxLevel);
  void setSinkLevel(Sink sink, LogLevel maxLevel);
  void removeSink(Sink sink);

  void tick(uint32_t now);
  void vlog(LogLevel level, const char *tag, const char *fmt, va_list ap);
  bool enabled(LogLevel level) const {
    return level != LogLevel::Off && level <= level_;
  }

  // Replay buffered lines oldest → newest.
  size_t dump(DumpFn fn, void *ctx) const;
  size_t count() const { return count_; }
  void clear();

private:
  struct SinkEntry {
    Sink fn = nullptr;
    void *ctx = nullptr;
    LogLevel maxLevel = LogLevel::Off;
  };

  UptimeFn uptime_ = nullptr;
  WallClockFn wall_ = nullptr;
  LogLevel level_ = LogLevel::Info;
  LogLevel baseLevel_ = LogLevel::Info;
  uint32_t revertAt_ = 0;
  SinkEntry sinks_[kMaxSinks];
  char ring_[kLines][kLineLen];
  size_t head_ = 0; // next write position
  size_t count_ = 0;
};

Logger &logger();

} // namespace edk
