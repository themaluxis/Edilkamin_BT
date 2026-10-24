#include "Md5.h"
#include <cstring>

namespace edk {

static const uint32_t kK[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

static const uint8_t kS[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7,
                               12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,
                               14, 20, 5,  9,  14, 20, 4, 11, 16, 23, 4, 11, 16,
                               23, 4,  11, 16, 23, 4,  11, 16, 23, 6, 10, 15, 21,
                               6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21};

static inline uint32_t rotl(uint32_t x, uint8_t c) {
  return (x << c) | (x >> (32 - c));
}

void Md5::reset() {
  state_[0] = 0x67452301;
  state_[1] = 0xefcdab89;
  state_[2] = 0x98badcfe;
  state_[3] = 0x10325476;
  bits_ = 0;
  bufLen_ = 0;
}

void Md5::transform(const uint8_t block[64]) {
  uint32_t m[16];
  for (int i = 0; i < 16; i++)
    m[i] = (uint32_t)block[i * 4] | ((uint32_t)block[i * 4 + 1] << 8) |
           ((uint32_t)block[i * 4 + 2] << 16) |
           ((uint32_t)block[i * 4 + 3] << 24);
  uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
  for (int i = 0; i < 64; i++) {
    uint32_t f;
    int g;
    if (i < 16) {
      f = (b & c) | (~b & d);
      g = i;
    } else if (i < 32) {
      f = (d & b) | (~d & c);
      g = (5 * i + 1) & 15;
    } else if (i < 48) {
      f = b ^ c ^ d;
      g = (3 * i + 5) & 15;
    } else {
      f = c ^ (b | ~d);
      g = (7 * i) & 15;
    }
    f = f + a + kK[i] + m[g];
    a = d;
    d = c;
    c = b;
    b = b + rotl(f, kS[i]);
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
}

void Md5::update(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  bits_ += (uint64_t)len * 8;
  while (len > 0) {
    size_t take = 64 - bufLen_;
    if (take > len)
      take = len;
    memcpy(buf_ + bufLen_, p, take);
    bufLen_ += take;
    p += take;
    len -= take;
    if (bufLen_ == 64) {
      transform(buf_);
      bufLen_ = 0;
    }
  }
}

void Md5::update(const char *s) { update(s, s ? strlen(s) : 0); }

void Md5::finish(uint8_t digest[16]) {
  uint64_t bits = bits_;
  uint8_t pad = 0x80;
  update(&pad, 1);
  uint8_t zero = 0;
  while (bufLen_ != 56)
    update(&zero, 1);
  uint8_t lenBytes[8];
  for (int i = 0; i < 8; i++)
    lenBytes[i] = (uint8_t)(bits >> (8 * i));
  update(lenBytes, 8);
  for (int i = 0; i < 4; i++) {
    digest[i * 4] = (uint8_t)(state_[i]);
    digest[i * 4 + 1] = (uint8_t)(state_[i] >> 8);
    digest[i * 4 + 2] = (uint8_t)(state_[i] >> 16);
    digest[i * 4 + 3] = (uint8_t)(state_[i] >> 24);
  }
  reset();
}

void Md5::toHex(const uint8_t digest[16], char out[33]) {
  static const char *hexd = "0123456789abcdef";
  for (int i = 0; i < 16; i++) {
    out[i * 2] = hexd[digest[i] >> 4];
    out[i * 2 + 1] = hexd[digest[i] & 15];
  }
  out[32] = '\0';
}

void Md5::hex(const void *data, size_t len, char out[33]) {
  Md5 h;
  h.update(data, len);
  uint8_t d[16];
  h.finish(d);
  toHex(d, out);
}

} // namespace edk
