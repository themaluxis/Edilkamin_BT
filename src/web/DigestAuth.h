// HTTP Digest authentication (RFC 2617, MD5 / qop=auth) for the local web
// administration (F05). Pure C++: header parsing, HA1 derivation, response
// verification, nonce slots and a brute-force throttle. Only the password hash
// HA1 = MD5(user:realm:password) is ever stored on the device.
#pragma once
#include <cstddef>
#include <cstdint>

namespace edk {

static constexpr const char *kAuthRealm = "Edilkamin";

struct DigestRequest {
  char username[33] = "";
  char realm[33] = "";
  char nonce[65] = "";
  char uri[160] = "";
  char response[33] = "";
  char qop[9] = "";
  char nc[9] = "";
  char cnonce[65] = "";
  char opaque[33] = "";
  char algorithm[12] = "";
};

// Parses the value of an Authorization header ("Digest username=..., ...").
// Returns false when the scheme is not Digest or a required field is missing.
bool parseDigestHeader(const char *header, DigestRequest &out);

// HA1 for the given credentials (lowercase hex, 32 chars + NUL).
void digestHa1(const char *user, const char *realm, const char *password,
               char out[33]);

// Expected "response" for a request, given HA1 and the HTTP method.
void digestExpectedResponse(const char *ha1, const char *method,
                            const DigestRequest &req, char out[33]);

// Full verification: user / realm / uri path match and response matches.
// `requestPath` is the path the server actually received (query excluded).
bool digestVerify(const char *ha1, const char *user, const char *realm,
                  const char *method, const char *requestPath,
                  const DigestRequest &req);

// Nonces derived from a boot-time secret and a time slot; the current and the
// previous slot are accepted so a nonce stays valid for 5 to 10 minutes.
class NonceSource {
public:
  static constexpr uint32_t kSlotMs = 5UL * 60UL * 1000UL;
  void begin(uint32_t secretA, uint32_t secretB) {
    secretA_ = secretA;
    secretB_ = secretB;
  }
  void current(uint32_t nowMs, char out[33]) const { make(slot(nowMs), out); }
  bool accepts(const char *nonce, uint32_t nowMs) const;

private:
  uint32_t slot(uint32_t nowMs) const { return nowMs / kSlotMs; }
  void make(uint32_t slot, char out[33]) const;
  uint32_t secretA_ = 0x5eed1234;
  uint32_t secretB_ = 0x9abcdef0;
};

// Brute-force throttle: after kMaxFailures within kWindowMs, requests are
// refused for kLockoutMs. Any success clears the counter.
class AuthThrottle {
public:
  static constexpr uint8_t kMaxFailures = 5;
  static constexpr uint32_t kWindowMs = 60000;
  static constexpr uint32_t kLockoutMs = 30000;

  bool locked(uint32_t nowMs) const {
    return lockedUntil_ != 0 && (int32_t)(lockedUntil_ - nowMs) > 0;
  }
  uint32_t retryAfterS(uint32_t nowMs) const {
    return locked(nowMs) ? (lockedUntil_ - nowMs + 999) / 1000 : 0;
  }
  void onFailure(uint32_t nowMs);
  void onSuccess() {
    failures_ = 0;
    lockedUntil_ = 0;
  }
  uint8_t failures() const { return failures_; }

private:
  uint8_t failures_ = 0;
  uint32_t firstFailureAt_ = 0;
  uint32_t lockedUntil_ = 0;
};

} // namespace edk
