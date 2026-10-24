#include "Logger.h"
#include <cstdio>
#include <cstring>

namespace edk {

Logger &logger() {
  static Logger instance;
  return instance;
}

const char *logLevelName(LogLevel level) {
  switch (level) {
  case LogLevel::Off:
    return "OFF";
  case LogLevel::Error:
    return "ERROR";
  case LogLevel::Warn:
    return "WARN";
  case LogLevel::Info:
    return "INFO";
  case LogLevel::Debug:
    return "DEBUG";
  case LogLevel::Trace:
    return "TRACE";
  }
  return "?";
}

bool parseLogLevel(const char *text, LogLevel &out) {
  if (!text)
    return false;
  static const LogLevel levels[] = {LogLevel::Off,  LogLevel::Error,
                                    LogLevel::Warn, LogLevel::Info,
                                    LogLevel::Debug, LogLevel::Trace};
  for (LogLevel l : levels) {
    const char *name = logLevelName(l);
    size_t n = strlen(name);
    bool same = strlen(text) == n;
    for (size_t i = 0; same && i < n; i++) {
      char a = text[i], b = name[i];
      if (a >= 'a' && a <= 'z')
        a = (char)(a - 'a' + 'A');
      same = a == b;
    }
    if (same) {
      out = l;
      return true;
    }
  }
  return false;
}

void logf(LogLevel level, const char *tag, const char *fmt, ...) {
  Logger &l = logger();
  if (!l.enabled(level))
    return;
  va_list ap;
  va_start(ap, fmt);
  l.vlog(level, tag, fmt, ap);
  va_end(ap);
}

void Logger::begin(UptimeFn uptime, WallClockFn wall) {
  uptime_ = uptime;
  wall_ = wall;
}

void Logger::setLevel(LogLevel level, uint32_t revertAfterMs) {
  if (revertAfterMs > 0) {
    if (revertAt_ == 0)
      baseLevel_ = level_;
    revertAt_ = uptime_ ? uptime_() + revertAfterMs : 1;
  } else {
    baseLevel_ = level;
    revertAt_ = 0;
  }
  level_ = level;
}

bool Logger::addSink(Sink sink, void *ctx, LogLevel maxLevel) {
  for (auto &s : sinks_) {
    if (s.fn == nullptr) {
      s.fn = sink;
      s.ctx = ctx;
      s.maxLevel = maxLevel;
      return true;
    }
  }
  return false;
}

void Logger::setSinkLevel(Sink sink, LogLevel maxLevel) {
  for (auto &s : sinks_)
    if (s.fn == sink)
      s.maxLevel = maxLevel;
}

void Logger::removeSink(Sink sink) {
  for (auto &s : sinks_)
    if (s.fn == sink)
      s = SinkEntry();
}

void Logger::tick(uint32_t now) {
  if (revertAt_ != 0 && (int32_t)(now - revertAt_) >= 0) {
    revertAt_ = 0;
    level_ = baseLevel_;
    EDK_LOGI("LOG", "temporary level expired, back to %s",
             logLevelName(level_));
  }
}

void Logger::vlog(LogLevel level, const char *tag, const char *fmt,
                  va_list ap) {
  char *line = ring_[head_];
  // Reserve the slot first: a sink may log again (nested), which must land
  // in the next slot instead of overwriting the line being emitted.
  head_ = (head_ + 1) % kLines;
  if (count_ < kLines)
    count_++;

  size_t pos = 0;
  char wall[12];
  if (wall_ && wall_(wall, sizeof(wall)))
    pos += (size_t)snprintf(line + pos, kLineLen - pos, "%s ", wall);
  uint32_t up = uptime_ ? uptime_() : 0;
  pos += (size_t)snprintf(line + pos, kLineLen - pos, "[%6lu.%03lu] %c %s: ",
                          (unsigned long)(up / 1000),
                          (unsigned long)(up % 1000), logLevelName(level)[0],
                          tag ? tag : "-");
  if (pos < kLineLen - 1)
    vsnprintf(line + pos, kLineLen - pos, fmt, ap);
  line[kLineLen - 1] = '\0';

  for (auto &s : sinks_)
    if (s.fn && level <= s.maxLevel)
      s.fn(level, line, s.ctx);
}

size_t Logger::dump(DumpFn fn, void *ctx) const {
  size_t start = (head_ + kLines - count_) % kLines;
  for (size_t i = 0; i < count_; i++)
    fn(ring_[(start + i) % kLines], ctx);
  return count_;
}

void Logger::clear() {
  head_ = 0;
  count_ = 0;
}

} // namespace edk
