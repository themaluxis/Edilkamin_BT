#include "DigestAuth.h"
#include "core/Md5.h"
#include "core/Strings.h"
#include <cctype>
#include <cstdio>
#include <cstring>

namespace edk {

namespace {

bool ieq(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
      return false;
    if (a[i] == '\0')
      return true;
  }
  return true;
}

// Reads one `key=value` pair (value quoted or bare) starting at *p.
// Advances *p past the pair and the following comma.
bool nextPair(const char **p, char *key, size_t keyLen, char *val,
              size_t valLen) {
  const char *s = *p;
  while (*s == ' ' || *s == ',' || *s == '\t')
    s++;
  if (*s == '\0')
    return false;
  size_t k = 0;
  while (*s && *s != '=' && *s != ',' && *s != ' ') {
    if (k + 1 < keyLen)
      key[k++] = (char)tolower((unsigned char)*s);
    s++;
  }
  key[k] = '\0';
  while (*s == ' ')
    s++;
  if (*s != '=') {
    // malformed token: skip to next comma
    while (*s && *s != ',')
      s++;
    *p = s;
    val[0] = '\0';
    return k > 0;
  }
  s++;
  while (*s == ' ')
    s++;
  size_t v = 0;
  if (*s == '"') {
    s++;
    while (*s && *s != '"') {
      char c = *s;
      if (c == '\\' && s[1]) {
        s++;
        c = *s;
      }
      if (v + 1 < valLen)
        val[v++] = c;
      s++;
    }
    if (*s == '"')
      s++;
  } else {
    while (*s && *s != ',' && *s != ' ') {
      if (v + 1 < valLen)
        val[v++] = *s;
      s++;
    }
  }
  val[v] = '\0';
  while (*s && *s != ',')
    s++;
  *p = s;
  return true;
}

} // namespace

bool parseDigestHeader(const char *header, DigestRequest &out) {
  out = DigestRequest();
  if (!header)
    return false;
  while (*header == ' ')
    header++;
  if (!ieq(header, "digest", 6) || !(header[6] == ' ' || header[6] == '\t'))
    return false;
  const char *p = header + 6;
  char key[16], val[160];
  while (nextPair(&p, key, sizeof(key), val, sizeof(val))) {
    if (strcmp(key, "username") == 0)
      copyStr(out.username, sizeof(out.username), val);
    else if (strcmp(key, "realm") == 0)
      copyStr(out.realm, sizeof(out.realm), val);
    else if (strcmp(key, "nonce") == 0)
      copyStr(out.nonce, sizeof(out.nonce), val);
    else if (strcmp(key, "uri") == 0)
      copyStr(out.uri, sizeof(out.uri), val);
    else if (strcmp(key, "response") == 0)
      copyStr(out.response, sizeof(out.response), val);
    else if (strcmp(key, "qop") == 0)
      copyStr(out.qop, sizeof(out.qop), val);
    else if (strcmp(key, "nc") == 0)
      copyStr(out.nc, sizeof(out.nc), val);
    else if (strcmp(key, "cnonce") == 0)
      copyStr(out.cnonce, sizeof(out.cnonce), val);
    else if (strcmp(key, "opaque") == 0)
      copyStr(out.opaque, sizeof(out.opaque), val);
    else if (strcmp(key, "algorithm") == 0)
      copyStr(out.algorithm, sizeof(out.algorithm), val);
  }
  return out.username[0] && out.realm[0] && out.nonce[0] && out.uri[0] &&
         strlen(out.response) == 32;
}

void digestHa1(const char *user, const char *realm, const char *password,
               char out[33]) {
  Md5 h;
  h.update(user);
  h.update(":", 1);
  h.update(realm);
  h.update(":", 1);
  h.update(password);
  uint8_t d[16];
  h.finish(d);
  Md5::toHex(d, out);
}

void digestExpectedResponse(const char *ha1, const char *method,
                            const DigestRequest &req, char out[33]) {
  char ha2[33];
  {
    Md5 h;
    h.update(method);
    h.update(":", 1);
    h.update(req.uri);
    uint8_t d[16];
    h.finish(d);
    Md5::toHex(d, ha2);
  }
  Md5 h;
  h.update(ha1);
  h.update(":", 1);
  h.update(req.nonce);
  h.update(":", 1);
  if (req.qop[0]) {
    h.update(req.nc);
    h.update(":", 1);
    h.update(req.cnonce);
    h.update(":", 1);
    h.update(req.qop);
    h.update(":", 1);
  }
  h.update(ha2);
  uint8_t d[16];
  h.finish(d);
  Md5::toHex(d, out);
}

bool digestVerify(const char *ha1, const char *user, const char *realm,
                  const char *method, const char *requestPath,
                  const DigestRequest &req) {
  if (!ha1 || !ha1[0] || strcmp(req.username, user) != 0 ||
      strcmp(req.realm, realm) != 0)
    return false;
  if (req.algorithm[0] && !ieq(req.algorithm, "md5", 4))
    return false;
  if (req.qop[0] && strcmp(req.qop, "auth") != 0)
    return false;
  // The digest URI must designate the resource actually requested.
  if (requestPath && requestPath[0]) {
    size_t n = strlen(requestPath);
    if (strncmp(req.uri, requestPath, n) != 0 ||
        !(req.uri[n] == '\0' || req.uri[n] == '?'))
      return false;
  }
  char expected[33];
  digestExpectedResponse(ha1, method, req, expected);
  // constant-time compare
  unsigned diff = 0;
  for (int i = 0; i < 32; i++)
    diff |= (unsigned)(expected[i] ^ req.response[i]);
  return diff == 0;
}

void NonceSource::make(uint32_t slot, char out[33]) const {
  uint8_t in[12];
  for (int i = 0; i < 4; i++) {
    in[i] = (uint8_t)(secretA_ >> (8 * i));
    in[4 + i] = (uint8_t)(secretB_ >> (8 * i));
    in[8 + i] = (uint8_t)(slot >> (8 * i));
  }
  Md5::hex(in, sizeof(in), out);
}

bool NonceSource::accepts(const char *nonce, uint32_t nowMs) const {
  if (!nonce || strlen(nonce) != 32)
    return false;
  char cur[33], prev[33];
  uint32_t s = slot(nowMs);
  make(s, cur);
  if (strcmp(nonce, cur) == 0)
    return true;
  if (s == 0)
    return false;
  make(s - 1, prev);
  return strcmp(nonce, prev) == 0;
}

void AuthThrottle::onFailure(uint32_t nowMs) {
  if (failures_ == 0 || (uint32_t)(nowMs - firstFailureAt_) > kWindowMs) {
    failures_ = 0;
    firstFailureAt_ = nowMs;
  }
  if (failures_ < 255)
    failures_++;
  if (failures_ >= kMaxFailures) {
    lockedUntil_ = nowMs + kLockoutMs;
    if (lockedUntil_ == 0)
      lockedUntil_ = 1;
    failures_ = 0;
  }
}

} // namespace edk
