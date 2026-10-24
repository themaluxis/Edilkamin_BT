#include "config/AppConfig.h"
#include "config/ConfigJson.h"
#include "core/Md5.h"
#include "core/Strings.h"
#include "web/DigestAuth.h"
#include <ArduinoJson.h>
#include <cstring>
#include <string>
#include <unity.h>

using namespace edk;

void setUp() {}
void tearDown() {}

// ---- MD5 ------------------------------------------------------------------
static std::string md5s(const std::string &s) {
  char out[33];
  Md5::hex(s.data(), s.size(), out);
  return out;
}

void test_md5_vectors() {
  TEST_ASSERT_EQUAL_STRING("d41d8cd98f00b204e9800998ecf8427e", md5s("").c_str());
  TEST_ASSERT_EQUAL_STRING("900150983cd24fb0d6963f7d28e17f72", md5s("abc").c_str());
  TEST_ASSERT_EQUAL_STRING("9e107d9d372bb6826bd81d3542a419d6",
                           md5s("The quick brown fox jumps over the lazy dog").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "57edf4a22be3c955ac49da2e2107b67a",
      md5s("12345678901234567890123456789012345678901234567890123456789012345678901234567890").c_str());
  // incremental == one shot across block boundaries
  std::string big(1000, 'a');
  Md5 h;
  for (size_t i = 0; i < big.size(); i += 7)
    h.update(big.data() + i, std::min<size_t>(7, big.size() - i));
  uint8_t d[16];
  h.finish(d);
  char hex[33];
  Md5::toHex(d, hex);
  TEST_ASSERT_EQUAL_STRING(md5s(big).c_str(), hex);
}

// ---- Digest ---------------------------------------------------------------
// RFC 2617 §3.5 example
void test_digest_rfc_example() {
  const char *hdr =
      "Digest username=\"Mufasa\", realm=\"testrealm@host.com\", "
      "nonce=\"dcd98b7102dd2f0e8b11d0f600bfb0c093\", uri=\"/dir/index.html\", "
      "qop=auth, nc=00000001, cnonce=\"0a4f113b\", "
      "response=\"6629fae49393a05397450978507c4ef1\", "
      "opaque=\"5ccc069c403ebaf9f0171e9517f40e41\"";
  DigestRequest r;
  TEST_ASSERT_TRUE(parseDigestHeader(hdr, r));
  TEST_ASSERT_EQUAL_STRING("Mufasa", r.username);
  TEST_ASSERT_EQUAL_STRING("auth", r.qop);
  TEST_ASSERT_EQUAL_STRING("00000001", r.nc);
  char ha1[33];
  digestHa1("Mufasa", "testrealm@host.com", "Circle Of Life", ha1);
  TEST_ASSERT_EQUAL_STRING("939e7578ed9e3c518a452acee763bce9", ha1);
  char expected[33];
  digestExpectedResponse(ha1, "GET", r, expected);
  TEST_ASSERT_EQUAL_STRING("6629fae49393a05397450978507c4ef1", expected);
  TEST_ASSERT_TRUE(digestVerify(ha1, "Mufasa", "testrealm@host.com", "GET",
                                "/dir/index.html", r));
  // wrong method, wrong user, wrong path, wrong password
  TEST_ASSERT_FALSE(digestVerify(ha1, "Mufasa", "testrealm@host.com", "POST",
                                 "/dir/index.html", r));
  TEST_ASSERT_FALSE(digestVerify(ha1, "Simba", "testrealm@host.com", "GET",
                                 "/dir/index.html", r));
  TEST_ASSERT_FALSE(digestVerify(ha1, "Mufasa", "testrealm@host.com", "GET",
                                 "/api/config", r));
  char other[33];
  digestHa1("Mufasa", "testrealm@host.com", "wrong", other);
  TEST_ASSERT_FALSE(digestVerify(other, "Mufasa", "testrealm@host.com", "GET",
                                 "/dir/index.html", r));
  // empty HA1 (no password set) never authenticates
  TEST_ASSERT_FALSE(digestVerify("", "Mufasa", "testrealm@host.com", "GET",
                                 "/dir/index.html", r));
}

void test_digest_parse_edge_cases() {
  DigestRequest r;
  TEST_ASSERT_FALSE(parseDigestHeader("Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==", r));
  TEST_ASSERT_FALSE(parseDigestHeader(nullptr, r));
  TEST_ASSERT_FALSE(parseDigestHeader("Digest username=\"a\"", r)); // incomplete
  // quoted comma inside uri, escaped quote in username, uri with query
  const char *hdr =
      "digest  username=\"a\\\"b\",realm=\"Edilkamin\",nonce=\"n1\","
      "uri=\"/api/status?x=1,2\",response=\"00112233445566778899aabbccddeeff\","
      "algorithm=MD5";
  TEST_ASSERT_TRUE(parseDigestHeader(hdr, r));
  TEST_ASSERT_EQUAL_STRING("a\"b", r.username);
  TEST_ASSERT_EQUAL_STRING("/api/status?x=1,2", r.uri);
  TEST_ASSERT_EQUAL_STRING("MD5", r.algorithm);
  TEST_ASSERT_EQUAL_STRING("", r.qop);
  // oversize values are truncated, not overflowed
  std::string longHdr = "Digest username=\"" + std::string(300, 'u') +
                        "\", realm=\"r\", nonce=\"n\", uri=\"/\", response=\"" +
                        std::string(32, 'a') + "\"";
  TEST_ASSERT_TRUE(parseDigestHeader(longHdr.c_str(), r));
  TEST_ASSERT_EQUAL_UINT(32, strlen(r.username));
}

void test_digest_without_qop_and_path_check() {
  // RFC 2069 style (no qop): response = MD5(HA1:nonce:HA2)
  char ha1[33];
  digestHa1("admin", kAuthRealm, "secret", ha1);
  DigestRequest r;
  copyStr(r.username, sizeof(r.username), "admin");
  copyStr(r.realm, sizeof(r.realm), kAuthRealm);
  copyStr(r.nonce, sizeof(r.nonce), "abc");
  copyStr(r.uri, sizeof(r.uri), "/api/log?lines=10");
  char expected[33];
  digestExpectedResponse(ha1, "GET", r, expected);
  copyStr(r.response, sizeof(r.response), expected);
  TEST_ASSERT_TRUE(digestVerify(ha1, "admin", kAuthRealm, "GET", "/api/log", r));
  TEST_ASSERT_FALSE(digestVerify(ha1, "admin", kAuthRealm, "GET", "/api/lo", r));
  TEST_ASSERT_FALSE(digestVerify(ha1, "admin", kAuthRealm, "GET", "/api/log/x", r));
}

void test_nonce_slots() {
  NonceSource n;
  n.begin(0x12345678, 0x9abcdef0);
  char a[33], b[33];
  n.current(1000, a);
  n.current(2000, b);
  TEST_ASSERT_EQUAL_STRING(a, b); // same slot
  TEST_ASSERT_TRUE(n.accepts(a, 2000));
  // next slot: previous nonce still accepted, the one before is not
  n.current(NonceSource::kSlotMs + 10, b);
  TEST_ASSERT_TRUE(strcmp(a, b) != 0);
  TEST_ASSERT_TRUE(n.accepts(a, NonceSource::kSlotMs + 10));
  TEST_ASSERT_FALSE(n.accepts(a, 2 * NonceSource::kSlotMs + 10));
  TEST_ASSERT_FALSE(n.accepts("short", 10));
  // different secret -> different nonce
  NonceSource m;
  m.begin(1, 2);
  m.current(1000, b);
  TEST_ASSERT_TRUE(strcmp(a, b) != 0);
}

void test_auth_throttle() {
  AuthThrottle t;
  TEST_ASSERT_FALSE(t.locked(0));
  for (int i = 0; i < AuthThrottle::kMaxFailures - 1; i++)
    t.onFailure(1000 + i);
  TEST_ASSERT_FALSE(t.locked(2000));
  t.onFailure(2000);
  TEST_ASSERT_TRUE(t.locked(2001));
  TEST_ASSERT_TRUE(t.retryAfterS(2001) > 0 && t.retryAfterS(2001) <= 30);
  TEST_ASSERT_FALSE(t.locked(2000 + AuthThrottle::kLockoutMs + 1));
  // failures spread over more than the window do not lock
  AuthThrottle u;
  for (int i = 0; i < 10; i++)
    u.onFailure(i * (AuthThrottle::kWindowMs + 1));
  TEST_ASSERT_FALSE(u.locked(11 * (AuthThrottle::kWindowMs + 1)));
  // success clears
  AuthThrottle v;
  for (int i = 0; i < 4; i++)
    v.onFailure(10);
  v.onSuccess();
  v.onFailure(20);
  TEST_ASSERT_FALSE(v.locked(21));
}

// ---- Config JSON ----------------------------------------------------------
void test_config_json_roundtrip_and_secrets() {
  AppConfig a;
  copyStr(a.mqttServer, sizeof(a.mqttServer), "broker.local");
  copyStr(a.mqttPass, sizeof(a.mqttPass), "s3cret");
  copyStr(a.adminHash, sizeof(a.adminHash), "0123456789abcdef0123456789abcdef");
  copyStr(a.adminUser, sizeof(a.adminUser), "max");
  a.mqttPort = 8883;
  a.blePauseS = 0;
  a.ntpOffsetSec = -3600;
  a.webEnabled = false;
  a.logLevel = 5;

  JsonDocument doc;
  configToJson(a, doc.to<JsonObject>(), ConfigSecrets::Include);
  TEST_ASSERT_EQUAL_INT(AppConfig::kSchemaVersion, doc["schema"].as<int>());
  TEST_ASSERT_EQUAL_STRING("s3cret", doc["mqtt_pass"].as<const char *>());

  AppConfig b;
  ConfigApplyResult r = configFromJson(doc.as<JsonObjectConst>(), b, true);
  TEST_ASSERT_EQUAL_INT(0, r.unknown);
  TEST_ASSERT_EQUAL_INT(0, r.ignored);
  TEST_ASSERT_EQUAL_UINT8(AppConfig::kSchemaVersion, r.schema);
  TEST_ASSERT_EQUAL_STRING("broker.local", b.mqttServer);
  TEST_ASSERT_EQUAL_STRING("s3cret", b.mqttPass);
  TEST_ASSERT_EQUAL_STRING("max", b.adminUser);
  TEST_ASSERT_EQUAL_STRING(a.adminHash, b.adminHash);
  TEST_ASSERT_EQUAL_UINT16(8883, b.mqttPort);
  TEST_ASSERT_EQUAL_UINT16(0, b.blePauseS);
  TEST_ASSERT_EQUAL(-3600, b.ntpOffsetSec);
  TEST_ASSERT_FALSE(b.webEnabled);
  TEST_ASSERT_EQUAL_UINT8(5, b.logLevel);

  // export omits secrets entirely
  JsonDocument exp;
  configToJson(a, exp.to<JsonObject>(), ConfigSecrets::Omit);
  TEST_ASSERT_TRUE(exp["mqtt_pass"].isNull());
  TEST_ASSERT_TRUE(exp["admin_hash"].isNull());
  TEST_ASSERT_FALSE(exp["mqtt_user"].isNull());
  std::string text;
  serializeJson(exp, text);
  TEST_ASSERT_NULL(strstr(text.c_str(), "s3cret"));
  TEST_ASSERT_NULL(strstr(text.c_str(), "0123456789abcdef"));

  // import never accepts secrets, counts unknown keys, keeps untouched fields
  AppConfig c;
  copyStr(c.mqttPass, sizeof(c.mqttPass), "keep");
  JsonDocument in;
  deserializeJson(in, "{\"mqtt_pass\":\"evil\",\"admin_hash\":\"x\","
                      "\"mqtt_server\":\"h\",\"bogus\":1,\"ble_session_s\":\"45\"}");
  r = configFromJson(in.as<JsonObjectConst>(), c, false);
  TEST_ASSERT_EQUAL_INT(2, r.ignored);
  TEST_ASSERT_EQUAL_INT(1, r.unknown);
  TEST_ASSERT_EQUAL_INT(2, r.applied);
  TEST_ASSERT_EQUAL_STRING("keep", c.mqttPass);
  TEST_ASSERT_EQUAL_STRING("h", c.mqttServer);
  TEST_ASSERT_EQUAL_UINT16(45, c.bleSessionS); // legacy string number
  TEST_ASSERT_EQUAL_UINT8(1, r.schema);          // no schema key -> v1
  TEST_ASSERT_TRUE(configKeyIsSecret("mqtt_pass"));
  TEST_ASSERT_FALSE(configKeyIsSecret("mqtt_user"));
}

void test_config_json_legacy_and_bad_types() {
  AppConfig c;
  JsonDocument in;
  deserializeJson(in, "{\"mqtt_port\":\"1884\",\"ble_enabled\":\"false\","
                      "\"telnet_enabled\":1,\"log_level\":\"abc\","
                      "\"mqtt_server\":42,\"ble_poll_ms\":99999}");
  ConfigApplyResult r = configFromJson(in.as<JsonObjectConst>(), c, true);
  TEST_ASSERT_EQUAL_UINT16(1884, c.mqttPort);
  TEST_ASSERT_FALSE(c.bleEnabled);
  TEST_ASSERT_TRUE(c.telnetEnabled);
  TEST_ASSERT_EQUAL_UINT8(3, c.logLevel);          // unparsable -> unchanged
  TEST_ASSERT_EQUAL_STRING("", c.mqttServer);      // wrong type -> unchanged
  TEST_ASSERT_EQUAL_UINT16(65535, c.blePollIntervalMs); // saturated, clamped later
  TEST_ASSERT_EQUAL_INT(4, r.applied);
  char notes[128];
  validateConfig(c, notes, sizeof(notes));
  TEST_ASSERT_EQUAL_UINT16(60000, c.blePollIntervalMs);
}

void test_config_validation_hostname() {
  AppConfig c;
  copyStr(c.hostname, sizeof(c.hostname), " My Stove_ESP.1 ");
  int fixes = validateConfig(c, nullptr, 0);
  TEST_ASSERT_TRUE(fixes >= 1);
  TEST_ASSERT_EQUAL_STRING("my-stove-esp-1", c.hostname);
  copyStr(c.hostname, sizeof(c.hostname), "--@@--");
  validateConfig(c, nullptr, 0);
  TEST_ASSERT_EQUAL_STRING("edilkaminble", c.hostname);
  copyStr(c.hostname, sizeof(c.hostname), "edilkamin-a1b2c3");
  TEST_ASSERT_EQUAL_INT(0, validateConfig(c, nullptr, 0));
  // identity bookkeeping and feature flags round-trip through JSON
  AppConfig a;
  copyStr(a.haPublishedId, sizeof(a.haPublishedId), "322707E4");
  copyStr(a.haPublishedPrefix, sizeof(a.haPublishedPrefix), "edilkamin");
  a.featRelax = false;
  JsonDocument doc;
  configToJson(a, doc.to<JsonObject>(), ConfigSecrets::Omit);
  AppConfig b;
  configFromJson(doc.as<JsonObjectConst>(), b, false);
  TEST_ASSERT_EQUAL_STRING("322707E4", b.haPublishedId);
  TEST_ASSERT_FALSE(b.featRelax);
  TEST_ASSERT_TRUE(b.featStandby);
}

void test_config_validation_admin_fields() {
  AppConfig c;
  copyStr(c.adminUser, sizeof(c.adminUser), " ad:min ");
  copyStr(c.adminHash, sizeof(c.adminHash), "nothex");
  char notes[128];
  int fixes = validateConfig(c, notes, sizeof(notes));
  TEST_ASSERT_TRUE(fixes >= 2);
  TEST_ASSERT_EQUAL_STRING("ad_min", c.adminUser);
  TEST_ASSERT_EQUAL_STRING("", c.adminHash);
  TEST_ASSERT_NOT_NULL(strstr(notes, "admin_hash"));
  AppConfig d;
  d.adminUser[0] = '\0';
  copyStr(d.adminHash, sizeof(d.adminHash), "0123456789ABCDEF0123456789abcdef");
  fixes = validateConfig(d, nullptr, 0);
  TEST_ASSERT_EQUAL_INT(1, fixes);
  TEST_ASSERT_EQUAL_STRING("admin", d.adminUser);
  TEST_ASSERT_EQUAL_STRING("0123456789ABCDEF0123456789abcdef", d.adminHash);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_md5_vectors);
  RUN_TEST(test_digest_rfc_example);
  RUN_TEST(test_digest_parse_edge_cases);
  RUN_TEST(test_digest_without_qop_and_path_check);
  RUN_TEST(test_nonce_slots);
  RUN_TEST(test_auth_throttle);
  RUN_TEST(test_config_json_roundtrip_and_secrets);
  RUN_TEST(test_config_json_legacy_and_bad_types);
  RUN_TEST(test_config_validation_hostname);
  RUN_TEST(test_config_validation_admin_fields);
  return UNITY_END();
}
