#include "OtaImage.h"
#include "core/Strings.h"
#include <cstdio>
#include <cstring>

namespace edk {

const char *otaCheckImageHeader(const uint8_t *buf, size_t n,
                                OtaHeaderInfo *info) {
  if (!buf || n < kOtaHeaderBytes)
    return "image too short";
  if (buf[0] != 0xE9)
    return "not an ESP32 application image (bad magic)";
  OtaHeaderInfo local;
  OtaHeaderInfo &i = info ? *info : local;
  i.segments = buf[1];
  i.chipId = (uint16_t)(buf[12] | (buf[13] << 8));
  i.hashAppended = buf[23] == 1;
  // esp_image_header_t (24) + first esp_image_segment_header_t (8) + app desc
  uint32_t magic = (uint32_t)buf[32] | ((uint32_t)buf[33] << 8) |
                   ((uint32_t)buf[34] << 16) | ((uint32_t)buf[35] << 24);
  i.appDescFound = magic == 0xABCD5432UL;
  if (i.chipId != 0x0000)
    return "image built for another chip (expected ESP32)";
  if (i.segments == 0 || i.segments > 16)
    return "implausible segment count";
  if (!i.appDescFound)
    return "application descriptor missing (not an application image)";
  return nullptr;
}

const char *otaMarkerPrefix(char *out, size_t n) {
  snprintf(out, n, "%s%s", "EDILKAMIN", "_BT_FW_V1:");
  return out;
}

void OtaMarkerScanner::begin(const char *needle) {
  needle_ = needle ? needle : "";
  needleLen_ = strlen(needle_);
  matched_ = 0;
  collecting_ = false;
  done_ = false;
  vlen_ = 0;
  version_[0] = '\0';
}

void OtaMarkerScanner::feed(const uint8_t *data, size_t n) {
  if (done_ || needleLen_ == 0)
    return;
  for (size_t k = 0; k < n; k++) {
    char c = (char)data[k];
    if (collecting_) {
      if (c == '\0' || vlen_ + 1 >= kMaxVersion) {
        version_[vlen_] = '\0';
        done_ = true;
        return;
      }
      version_[vlen_++] = c;
      continue;
    }
    if (c == needle_[matched_]) {
      if (++matched_ == needleLen_) {
        collecting_ = true;
        vlen_ = 0;
      }
    } else {
      // first character of the needle is unique, so a one-step restart is exact
      matched_ = (c == needle_[0]) ? 1 : 0;
    }
  }
}

} // namespace edk
