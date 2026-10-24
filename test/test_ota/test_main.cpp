#include "ota/OtaImage.h"
#include <cstring>
#include <string>
#include <unity.h>
#include <vector>

using namespace edk;

void setUp() {}
void tearDown() {}

static std::vector<uint8_t> goodHeader() {
  std::vector<uint8_t> h(128, 0);
  h[0] = 0xE9;
  h[1] = 5;     // segments
  h[12] = 0x00; // chip id ESP32
  h[13] = 0x00;
  h[23] = 1;    // hash appended
  h[32] = 0x32; // app desc magic 0xABCD5432 little endian
  h[33] = 0x54;
  h[34] = 0xCD;
  h[35] = 0xAB;
  return h;
}

void test_header_ok() {
  auto h = goodHeader();
  OtaHeaderInfo i;
  TEST_ASSERT_NULL(otaCheckImageHeader(h.data(), h.size(), &i));
  TEST_ASSERT_EQUAL_UINT16(0, i.chipId);
  TEST_ASSERT_EQUAL_UINT8(5, i.segments);
  TEST_ASSERT_TRUE(i.hashAppended);
  TEST_ASSERT_TRUE(i.appDescFound);
}

void test_header_rejections() {
  auto h = goodHeader();
  TEST_ASSERT_NOT_NULL(otaCheckImageHeader(h.data(), 10, nullptr)); // too short
  h[0] = 0xFF;
  TEST_ASSERT_NOT_NULL(strstr(otaCheckImageHeader(h.data(), h.size()), "magic"));
  h = goodHeader();
  h[12] = 0x09; // ESP32-S3
  TEST_ASSERT_NOT_NULL(strstr(otaCheckImageHeader(h.data(), h.size()), "chip"));
  h = goodHeader();
  h[33] = 0x00;
  TEST_ASSERT_NOT_NULL(strstr(otaCheckImageHeader(h.data(), h.size()), "descriptor"));
  h = goodHeader();
  h[1] = 0;
  TEST_ASSERT_NOT_NULL(strstr(otaCheckImageHeader(h.data(), h.size()), "segment"));
  // a LittleFS / random blob is refused
  std::vector<uint8_t> blob(128, 0xFF);
  TEST_ASSERT_NOT_NULL(otaCheckImageHeader(blob.data(), blob.size()));
}

void test_marker_prefix_is_not_self_matching_literal() {
  char p[32];
  otaMarkerPrefix(p, sizeof(p));
  TEST_ASSERT_EQUAL_STRING("EDILKAMIN_BT_FW_V1:", p);
  // first character unique in the needle (scanner restart invariant)
  TEST_ASSERT_NULL(strchr(p + 1, p[0]));
}

static std::string scan(const std::string &image, size_t chunk) {
  char p[32];
  OtaMarkerScanner s;
  s.begin(otaMarkerPrefix(p, sizeof(p)));
  for (size_t i = 0; i < image.size(); i += chunk)
    s.feed((const uint8_t *)image.data() + i, std::min(chunk, image.size() - i));
  return s.found() ? s.version() : std::string("<none>");
}

void test_marker_scanner_across_chunks() {
  std::string marker = std::string("EDILKAMIN_BT_FW_V1:") + "v1.4.0-3-gabc" + '\0';
  std::string image = std::string(1000, 'x') + "EDILKAM" + std::string(50, 'E') +
                      "EDILKAMIN_BT_F" + marker + std::string(500, '\xff');
  for (size_t chunk : {1u, 7u, 64u, 1436u, 4096u})
    TEST_ASSERT_EQUAL_STRING("v1.4.0-3-gabc", scan(image, chunk).c_str());
  TEST_ASSERT_EQUAL_STRING("<none>", scan(std::string(3000, 'a'), 100).c_str());
  // a marker with an empty version is found with ""
  std::string empty = std::string("EDILKAMIN_BT_FW_V1:") + '\0';
  TEST_ASSERT_EQUAL_STRING("", scan(std::string(10, 'q') + empty, 3).c_str());
  // version longer than the buffer is truncated, not overflowed
  std::string longv = "EDILKAMIN_BT_FW_V1:" + std::string(200, 'v');
  std::string got = scan(longv, 11);
  TEST_ASSERT_EQUAL_UINT(OtaMarkerScanner::kMaxVersion - 1, got.size());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_header_ok);
  RUN_TEST(test_header_rejections);
  RUN_TEST(test_marker_prefix_is_not_self_matching_literal);
  RUN_TEST(test_marker_scanner_across_chunks);
  return UNITY_END();
}
