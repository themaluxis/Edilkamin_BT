// Compact MD5 (RFC 1321) used for HTTP Digest authentication. Pure C++ so the
// web authentication logic can be unit tested natively.
#pragma once
#include <cstddef>
#include <cstdint>

namespace edk {

class Md5 {
public:
  Md5() { reset(); }
  void reset();
  void update(const void *data, size_t len);
  void update(const char *s);
  void finish(uint8_t digest[16]);

  // Lowercase hex of the digest (32 chars + NUL).
  static void toHex(const uint8_t digest[16], char out[33]);
  // One-shot helper: hex MD5 of a buffer.
  static void hex(const void *data, size_t len, char out[33]);

private:
  void transform(const uint8_t block[64]);
  uint32_t state_[4];
  uint64_t bits_;
  uint8_t buf_[64];
  size_t bufLen_;
};

} // namespace edk
