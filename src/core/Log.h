// Logging facade usable from pure (platform independent) modules.
// The implementation lives in logging/Logger.cpp.
#pragma once
#include <cstdarg>
#include <cstdint>

namespace edk {

enum class LogLevel : uint8_t { Off = 0, Error, Warn, Info, Debug, Trace };

const char *logLevelName(LogLevel level);
bool parseLogLevel(const char *text, LogLevel &out);

void logf(LogLevel level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

} // namespace edk

#define EDK_LOGE(tag, ...) ::edk::logf(::edk::LogLevel::Error, tag, __VA_ARGS__)
#define EDK_LOGW(tag, ...) ::edk::logf(::edk::LogLevel::Warn, tag, __VA_ARGS__)
#define EDK_LOGI(tag, ...) ::edk::logf(::edk::LogLevel::Info, tag, __VA_ARGS__)
#define EDK_LOGD(tag, ...) ::edk::logf(::edk::LogLevel::Debug, tag, __VA_ARGS__)
#define EDK_LOGT(tag, ...) ::edk::logf(::edk::LogLevel::Trace, tag, __VA_ARGS__)
