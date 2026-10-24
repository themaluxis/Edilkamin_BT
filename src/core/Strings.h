// Small bounded string helpers shared by pure and platform modules.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace edk {

// Bounded copy that always NUL-terminates. Returns false when truncated.
inline bool copyStr(char *dst, size_t dstSize, const char *src) {
  if (dst == nullptr || dstSize == 0)
    return false;
  if (src == nullptr) {
    dst[0] = '\0';
    return true;
  }
  size_t n = strlen(src);
  bool fits = n < dstSize;
  size_t copy = fits ? n : dstSize - 1;
  memcpy(dst, src, copy);
  dst[copy] = '\0';
  return fits;
}

// FNV-1a 32 bit hash, used for publish de-duplication.
inline uint32_t fnv1a(const char *s, size_t len) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; i++) {
    h ^= (uint8_t)s[i];
    h *= 16777619u;
  }
  return h;
}

inline uint32_t fnv1a(const char *s) { return fnv1a(s, strlen(s)); }

// Trim leading/trailing whitespace in place.
inline void trimInPlace(char *s) {
  if (!s)
    return;
  size_t len = strlen(s);
  size_t start = 0;
  while (start < len && (s[start] == ' ' || s[start] == '\t' ||
                         s[start] == '\r' || s[start] == '\n'))
    start++;
  size_t end = len;
  while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                         s[end - 1] == '\r' || s[end - 1] == '\n'))
    end--;
  if (start > 0)
    memmove(s, s + start, end - start);
  s[end - start] = '\0';
}

} // namespace edk
