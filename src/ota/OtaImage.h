// Firmware image checks for OTA uploads (F06). Pure C++ so they can be unit
// tested natively: ESP32 image header validation and a streaming search for
// the firmware marker that every Edilkamin_BT Enhanced build embeds.
#pragma once
#include <cstddef>
#include <cstdint>

namespace edk {

// Minimum number of leading bytes needed to validate an image header.
static constexpr size_t kOtaHeaderBytes = 64;

struct OtaHeaderInfo {
  uint16_t chipId = 0xFFFF;
  uint8_t segments = 0;
  bool hashAppended = false;
  bool appDescFound = false;
};

// Validates the start of an ESP32 application image (magic, chip id, app
// descriptor). Returns nullptr when acceptable, otherwise a short reason.
const char *otaCheckImageHeader(const uint8_t *buf, size_t n,
                                OtaHeaderInfo *info = nullptr);

// Builds the marker prefix at run time from two halves so the scanner's own
// needle never appears contiguously in the running image. Returns `out`.
const char *otaMarkerPrefix(char *out, size_t n);

// Streaming search for "<prefix><version>\0" across arbitrary chunk
// boundaries. The prefix must not contain its first character twice.
class OtaMarkerScanner {
public:
  static constexpr size_t kMaxVersion = 48;

  void begin(const char *needle);
  void feed(const uint8_t *data, size_t n);
  bool found() const { return done_; }
  const char *version() const { return done_ ? version_ : ""; }

private:
  const char *needle_ = "";
  size_t needleLen_ = 0;
  size_t matched_ = 0;
  bool collecting_ = false;
  bool done_ = false;
  size_t vlen_ = 0;
  char version_[kMaxVersion] = "";
};

} // namespace edk
