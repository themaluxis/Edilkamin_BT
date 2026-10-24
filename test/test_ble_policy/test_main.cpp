#include "ble/BleSessionPolicy.h"
#include <unity.h>

using namespace edk;

void setUp() {}
void tearDown() {}

static BlePolicyConfig cfg() {
  BlePolicyConfig c;
  c.sessionMs = 30000;
  c.pauseMs = 120000;
  c.reconnectInitialMs = 5000;
  c.reconnectMaxMs = 120000;
  c.scanTimeoutMs = 10000;
  c.connectedNoDataMs = 15000;
  c.minGapMs = 3000;
  return c;
}

static void bringUp(BleSessionPolicy &p, uint32_t &t) {
  p.setEnabled(true, t);
  TEST_ASSERT_EQUAL(BleState::Idle, p.state());
  TEST_ASSERT_EQUAL(BleAction::StartScan, p.tick(t));
  TEST_ASSERT_EQUAL(BleState::Scanning, p.state());
  t += 1000;
  p.onScanFound(t);
  TEST_ASSERT_EQUAL(BleState::Connecting, p.state());
  TEST_ASSERT_EQUAL(BleAction::None, p.tick(t));
  t += 2000;
  p.onConnectResult(true, BleErrorReason::None, t);
  TEST_ASSERT_EQUAL(BleState::Connected, p.state());
  TEST_ASSERT_TRUE(p.linkUp());
}

void test_nominal_cycle() {
  BleSessionPolicy p;
  p.configure(cfg());
  uint32_t t = 1000;
  TEST_ASSERT_EQUAL(BleState::Disabled, p.state());
  TEST_ASSERT_EQUAL(BleAction::None, p.tick(t));
  bringUp(p, t);
  t += 500;
  p.onValidResponse(t);
  TEST_ASSERT_EQUAL(BleState::Communicating, p.state());
  TEST_ASSERT_EQUAL_UINT32(1, p.stats().sessionsOk);
  // session lasts sessionMs then disconnects voluntarily
  TEST_ASSERT_EQUAL(BleAction::None, p.tick(t + 29000));
  TEST_ASSERT_EQUAL(BleAction::Disconnect, p.tick(t + 30000));
  TEST_ASSERT_EQUAL(BleState::Idle, p.state());
  // the disconnect we asked for is not an error
  p.onLinkLost(t + 30100);
  TEST_ASSERT_EQUAL(BleState::Idle, p.state());
  TEST_ASSERT_EQUAL_UINT32(0, p.stats().linkDrops);
  // paused for pauseMs
  TEST_ASSERT_EQUAL(BleAction::None, p.tick(t + 30000 + 119000));
  TEST_ASSERT_EQUAL(BleAction::StartScan, p.tick(t + 30000 + 120000));
  TEST_ASSERT_EQUAL(BleState::Scanning, p.state());
}

void test_user_request_shortens_pause_but_respects_gap() {
  BleSessionPolicy p;
  p.configure(cfg());
  uint32_t t = 1000;
  bringUp(p, t);
  p.onValidResponse(t);
  uint32_t endAt = t + 30000;
  TEST_ASSERT_EQUAL(BleAction::Disconnect, p.tick(endAt));
  // user command 1 s after the session ended: wait for the minimum gap
  p.requestSession(endAt + 1000);
  TEST_ASSERT_EQUAL(BleAction::None, p.tick(endAt + 1000));
  TEST_ASSERT_EQUAL(BleAction::StartScan, p.tick(endAt + 3000));
  // pending user command keeps the session open past sessionMs
  p.onScanFound(endAt + 3500);
  p.onConnectResult(true, BleErrorReason::None, endAt + 4000);
  p.onValidResponse(endAt + 4500);
  p.setUserPending(true);
  TEST_ASSERT_EQUAL(BleAction::None, p.tick(endAt + 4500 + 31000));
  p.setUserPending(false);
  p.setBusy(true);
  TEST_ASSERT_EQUAL(BleAction::None, p.tick(endAt + 4500 + 32000));
  p.setBusy(false);
  TEST_ASSERT_EQUAL(BleAction::Disconnect, p.tick(endAt + 4500 + 33000));
}

void test_scan_timeout_and_backoff() {
  BleSessionPolicy p;
  p.configure(cfg());
  uint32_t t = 1000;
  p.setEnabled(true, t);
  TEST_ASSERT_EQUAL(BleAction::StartScan, p.tick(t));
  TEST_ASSERT_EQUAL(BleAction::None, p.tick(t + 9999));
  TEST_ASSERT_EQUAL(BleAction::StopScan, p.tick(t + 10000));
  TEST_ASSERT_EQUAL(BleState::Error, p.state());
  TEST_ASSERT_EQUAL(BleErrorReason::NotFound, p.lastError());
  TEST_ASSERT_EQUAL_UINT32(5000, p.currentBackoffMs());
  uint32_t errAt = t + 10000;
  TEST_ASSERT_EQUAL(BleAction::None, p.tick(errAt + 4999));
  TEST_ASSERT_EQUAL(BleAction::StartScan, p.tick(errAt + 5000));
  // second failure doubles the delay, capped at reconnectMaxMs
  p.onScanFinished(errAt + 6000);
  TEST_ASSERT_EQUAL_UINT32(10000, p.currentBackoffMs());
  uint32_t now = errAt + 6000;
  for (int i = 0; i < 10; i++) {
    now += p.currentBackoffMs();
    TEST_ASSERT_EQUAL(BleAction::StartScan, p.tick(now));
    p.onScanFinished(now + 1);
    now += 1;
  }
  TEST_ASSERT_EQUAL_UINT32(120000, p.currentBackoffMs());
  TEST_ASSERT_EQUAL_UINT32(12, p.stats().scanNotFound);
  TEST_ASSERT_TRUE(p.unreachableForMs(now) > 0);
  // a successful session resets the backoff
  now += 120000;
  TEST_ASSERT_EQUAL(BleAction::StartScan, p.tick(now));
  p.onScanFound(now);
  p.onConnectResult(true, BleErrorReason::None, now);
  p.onValidResponse(now);
  TEST_ASSERT_EQUAL_UINT32(0, p.currentBackoffMs());
  TEST_ASSERT_EQUAL_UINT32(0, p.stats().consecutiveErrors);
  TEST_ASSERT_EQUAL_UINT32(0, p.unreachableForMs(now));
}

void test_connect_failure_and_link_drop() {
  BleSessionPolicy p;
  p.configure(cfg());
  uint32_t t = 0;
  p.setEnabled(true, t);
  p.tick(t);
  p.onScanFound(t);
  p.onConnectResult(false, BleErrorReason::ServiceMissing, t);
  TEST_ASSERT_EQUAL(BleState::Error, p.state());
  TEST_ASSERT_EQUAL(BleErrorReason::ServiceMissing, p.lastError());
  TEST_ASSERT_EQUAL_UINT32(1, p.stats().connectFailures);

  t += 5000;
  TEST_ASSERT_EQUAL(BleAction::StartScan, p.tick(t));
  p.onScanFound(t);
  p.onConnectResult(true, BleErrorReason::None, t);
  p.onValidResponse(t);
  TEST_ASSERT_EQUAL(BleState::Communicating, p.state());
  p.onLinkLost(t + 1000); // stove went away mid session
  TEST_ASSERT_EQUAL(BleState::Error, p.state());
  TEST_ASSERT_EQUAL(BleErrorReason::LinkDropped, p.lastError());
  TEST_ASSERT_EQUAL_UINT32(1, p.stats().linkDrops);
  TEST_ASSERT_FALSE(p.linkUp());
}

void test_connected_without_data() {
  BleSessionPolicy p;
  p.configure(cfg());
  uint32_t t = 0;
  bringUp(p, t);
  TEST_ASSERT_EQUAL(BleAction::None, p.tick(t + 14999));
  TEST_ASSERT_EQUAL(BleAction::Disconnect, p.tick(t + 15000));
  TEST_ASSERT_EQUAL(BleState::Error, p.state());
  TEST_ASSERT_EQUAL(BleErrorReason::NoData, p.lastError());
  p.onLinkLost(t + 15001); // expected
  TEST_ASSERT_EQUAL_UINT32(0, p.stats().linkDrops);
}

void test_disable_tears_down() {
  BleSessionPolicy p;
  p.configure(cfg());
  uint32_t t = 0;
  bringUp(p, t);
  p.onValidResponse(t);
  p.setEnabled(false, t);
  TEST_ASSERT_EQUAL(BleAction::Disconnect, p.tick(t));
  TEST_ASSERT_EQUAL(BleState::Disabled, p.state());
  TEST_ASSERT_EQUAL(BleAction::None, p.tick(t + 100000));
  TEST_ASSERT_EQUAL_UINT32(0, p.unreachableForMs(t + 100000)); // no watchdog
  p.onLinkLost(t + 1);
  TEST_ASSERT_EQUAL_UINT32(0, p.stats().linkDrops);
  // disable while scanning stops the scan
  BleSessionPolicy q;
  q.configure(cfg());
  q.setEnabled(true, 0);
  TEST_ASSERT_EQUAL(BleAction::StartScan, q.tick(0));
  q.setEnabled(false, 1);
  TEST_ASSERT_EQUAL(BleAction::StopScan, q.tick(1));
  TEST_ASSERT_EQUAL(BleState::Disabled, q.state());
  // re-enable starts a session immediately
  q.setEnabled(true, 2);
  TEST_ASSERT_EQUAL(BleAction::StartScan, q.tick(2));
}

void test_continuous_mode_never_disconnects() {
  BleSessionPolicy p;
  BlePolicyConfig c = cfg();
  c.pauseMs = 0;
  p.configure(c);
  uint32_t t = 0;
  bringUp(p, t);
  p.onValidResponse(t);
  TEST_ASSERT_EQUAL(BleAction::None, p.tick(t + 10 * 60 * 1000));
  TEST_ASSERT_EQUAL(BleState::Communicating, p.state());
}

void test_names() {
  TEST_ASSERT_EQUAL_STRING("COMMUNICATING", bleStateName(BleState::Communicating));
  TEST_ASSERT_EQUAL_STRING("stove not found", bleErrorReasonName(BleErrorReason::NotFound));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_nominal_cycle);
  RUN_TEST(test_user_request_shortens_pause_but_respects_gap);
  RUN_TEST(test_scan_timeout_and_backoff);
  RUN_TEST(test_connect_failure_and_link_drop);
  RUN_TEST(test_connected_without_data);
  RUN_TEST(test_disable_tears_down);
  RUN_TEST(test_continuous_mode_never_disconnects);
  RUN_TEST(test_names);
  return UNITY_END();
}
