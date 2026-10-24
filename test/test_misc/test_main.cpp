#include "config/AppConfig.h"
#include "core/Strings.h"
#include "diagnostics/Diagnostics.h"
#include "logging/Logger.h"
#include "mqtt/Topics.h"
#include <cstring>
#include <string>
#include <unity.h>
#include <vector>

using namespace edk;

void setUp() {}
void tearDown() {}

void test_config_validation_clamps() {
  AppConfig c;
  char notes[256];
  TEST_ASSERT_EQUAL_INT(0, validateConfig(c, notes, sizeof(notes)));
  c.bleSessionS = 1;
  c.blePauseS = 5;
  c.bleScanTimeoutS = 999;
  c.logLevel = 9;
  c.mqttPort = 0;
  c.dataMaxAgeS = 10;
  copyStr(c.stoveMac, sizeof(c.stoveMac), "zz:zz");
  copyStr(c.deviceId, sizeof(c.deviceId), "my/stove");
  int fixes = validateConfig(c, notes, sizeof(notes));
  TEST_ASSERT_TRUE(fixes >= 7);
  TEST_ASSERT_EQUAL_UINT16(10, c.bleSessionS);
  TEST_ASSERT_EQUAL_UINT16(10, c.blePauseS);
  TEST_ASSERT_EQUAL_UINT16(60, c.bleScanTimeoutS);
  TEST_ASSERT_EQUAL_UINT8(5, c.logLevel);
  TEST_ASSERT_EQUAL_UINT16(1883, c.mqttPort);
  TEST_ASSERT_EQUAL_UINT16(80, c.dataMaxAgeS); // session + pause + 60
  TEST_ASSERT_EQUAL_STRING("", c.stoveMac);
  TEST_ASSERT_EQUAL_STRING("my_stove", c.deviceId);
  TEST_ASSERT_NOT_NULL(strstr(notes, "ble_session_s"));
  // continuous mode (pause 0) stays allowed
  AppConfig d;
  d.blePauseS = 0;
  TEST_ASSERT_EQUAL_INT(0, validateConfig(d));
  TEST_ASSERT_EQUAL_UINT16(0, d.blePauseS);
}

void test_mac_normalisation() {
  char mac[18];
  copyStr(mac, sizeof(mac), "a4:c1:38:12:ab:ff");
  TEST_ASSERT_TRUE(normalizeMac(mac, sizeof(mac)));
  TEST_ASSERT_EQUAL_STRING("A4:C1:38:12:AB:FF", mac);
  copyStr(mac, sizeof(mac), "a4c13812abff");
  TEST_ASSERT_TRUE(normalizeMac(mac, sizeof(mac)));
  TEST_ASSERT_EQUAL_STRING("A4:C1:38:12:AB:FF", mac);
  copyStr(mac, sizeof(mac), "A4-C1-38-12-AB-FF");
  TEST_ASSERT_TRUE(normalizeMac(mac, sizeof(mac)));
  TEST_ASSERT_EQUAL_STRING("A4:C1:38:12:AB:FF", mac);
  copyStr(mac, sizeof(mac), "A4:C1:38:12:AB");
  TEST_ASSERT_FALSE(normalizeMac(mac, sizeof(mac)));
  TEST_ASSERT_FALSE(isValidMac("A4:C1:38:12:AB:GG"));
}

void test_strings() {
  char buf[5];
  TEST_ASSERT_TRUE(copyStr(buf, sizeof(buf), "abc"));
  TEST_ASSERT_EQUAL_STRING("abc", buf);
  TEST_ASSERT_FALSE(copyStr(buf, sizeof(buf), "abcdef"));
  TEST_ASSERT_EQUAL_STRING("abcd", buf);
  TEST_ASSERT_TRUE(copyStr(buf, sizeof(buf), nullptr));
  TEST_ASSERT_EQUAL_STRING("", buf);
  char t[16] = "  hi \r\n";
  trimInPlace(t);
  TEST_ASSERT_EQUAL_STRING("hi", t);
  TEST_ASSERT_NOT_EQUAL(fnv1a("a"), fnv1a("b"));
}

void test_topics() {
  Topics t("edilkamin", "322707E4");
  char buf[96];
  TEST_ASSERT_EQUAL_STRING("edilkamin/322707E4/target_temperature/state",
                           t.state(buf, sizeof(buf), "target_temperature"));
  TEST_ASSERT_EQUAL_STRING("edilkamin/322707E4/relax/set", t.set(buf, sizeof(buf), "relax"));
  TEST_ASSERT_EQUAL_STRING("edilkamin/322707E4/diagnostics/state",
                           t.make(buf, sizeof(buf), "diagnostics/state"));
  char entity[32];
  TEST_ASSERT_TRUE(t.parseSet("edilkamin/322707E4/hvac_mode/set", entity, sizeof(entity)));
  TEST_ASSERT_EQUAL_STRING("hvac_mode", entity);
  TEST_ASSERT_FALSE(t.parseSet("edilkamin/322707E4/hvac_mode/state", entity, sizeof(entity)));
  TEST_ASSERT_FALSE(t.parseSet("edilkamin/OTHER/hvac_mode/set", entity, sizeof(entity)));
  TEST_ASSERT_FALSE(t.parseSet("edilkamin/322707E4//set", entity, sizeof(entity)));
  TEST_ASSERT_FALSE(t.parseSet("homeassistant/status", entity, sizeof(entity)));
}

static uint32_t g_up = 0;
static uint32_t fakeUptime() { return g_up; }
static std::vector<std::string> g_lines;
static void captureSink(LogLevel, const char *line, void *) { g_lines.push_back(line); }
static void dumpCb(const char *line, void *ctx) {
  static_cast<std::vector<std::string> *>(ctx)->push_back(line);
}

void test_logger_levels_ring_and_revert() {
  Logger &l = logger();
  g_lines.clear();
  l.clear();
  l.begin(fakeUptime);
  l.addSink(captureSink, nullptr, LogLevel::Trace);
  l.setLevel(LogLevel::Info);
  g_up = 12345;
  EDK_LOGD("T", "hidden %d", 1);
  EDK_LOGI("T", "shown %d", 2);
  TEST_ASSERT_EQUAL_UINT32(1, g_lines.size());
  TEST_ASSERT_NOT_NULL(strstr(g_lines[0].c_str(), "[    12.345] I T: shown 2"));
  // temporary TRACE reverts to INFO
  l.setLevel(LogLevel::Trace, 1000);
  TEST_ASSERT_TRUE(l.temporary());
  EDK_LOGT("T", "trace");
  TEST_ASSERT_EQUAL_UINT32(2, g_lines.size());
  g_up = 13344;
  l.tick(g_up);
  TEST_ASSERT_EQUAL(LogLevel::Trace, l.level());
  g_up = 13345;
  l.tick(g_up);
  TEST_ASSERT_EQUAL(LogLevel::Info, l.level());
  TEST_ASSERT_FALSE(l.temporary());
  // ring buffer keeps the newest kLines
  for (int i = 0; i < 60; i++)
    EDK_LOGW("R", "line %d", i);
  std::vector<std::string> dumped;
  l.dump(dumpCb, &dumped);
  TEST_ASSERT_EQUAL_UINT32(Logger::kLines, dumped.size());
  TEST_ASSERT_NOT_NULL(strstr(dumped.back().c_str(), "line 59"));
  TEST_ASSERT_NOT_NULL(strstr(dumped.front().c_str(), "line 12"));
  l.removeSink(captureSink);
  LogLevel parsed;
  TEST_ASSERT_TRUE(parseLogLevel("debug", parsed));
  TEST_ASSERT_EQUAL(LogLevel::Debug, parsed);
  TEST_ASSERT_TRUE(parseLogLevel("OFF", parsed));
  TEST_ASSERT_FALSE(parseLogLevel("verbose", parsed));
  TEST_ASSERT_EQUAL_STRING("WARN", logLevelName(LogLevel::Warn));
}

void test_diagnostics_json() {
  DiagSnapshot s;
  s.firmware = "v1.1.0";
  s.bleState = "IDLE";
  s.lastError = "he said \"no\"\n";
  s.wifiRssi = -67;
  s.queueDepth = 2;
  s.queueCapacity = 16;
  char buf[1024];
  size_t n = buildDiagnosticsJson(s, buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0 && n < sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"fw\":\"v1.1.0\""));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"rssi\":-67"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"queue_depth\":2"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"last_error\":\"he said \\\"no\\\"\\n\""));
  TEST_ASSERT_EQUAL_CHAR('}', buf[n - 1]);
  ErrorTracker e;
  TEST_ASSERT_FALSE(e.has());
  e.set("boom", 5);
  TEST_ASSERT_TRUE(e.has());
  TEST_ASSERT_EQUAL_STRING("boom", e.message());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_config_validation_clamps);
  RUN_TEST(test_mac_normalisation);
  RUN_TEST(test_strings);
  RUN_TEST(test_topics);
  RUN_TEST(test_logger_levels_ring_and_revert);
  RUN_TEST(test_diagnostics_json);
  return UNITY_END();
}
