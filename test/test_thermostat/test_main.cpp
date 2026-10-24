#include "stove/ExternalThermostat.h"
#include <cstring>
#include <unity.h>

using namespace edk;

void setUp() {}
void tearDown() {}

static ExtThermostatConfig cfg() {
  ExtThermostatConfig c;
  c.enabled = true;
  c.setpointTenths = 200;
  c.hysteresisTenths = 5;
  c.sensorMaxAgeMs = 600000;
  c.minAdjustIntervalMs = 300000;
  return c;
}

static ExtInputs inputs(int16_t probe, int16_t setpoint) {
  ExtInputs i;
  i.stoveRoomKnown = true;
  i.stoveRoomTenths = probe;
  i.stoveSetpointKnown = true;
  i.stoveSetpointTenths = setpoint;
  return i;
}

void test_disabled_and_waiting_do_nothing() {
  ExternalThermostat t;
  ExtThermostatConfig c = cfg();
  c.enabled = false;
  t.configure(c);
  ExtAction a = t.tick(1000, inputs(210, 200));
  TEST_ASSERT_FALSE(a.write);
  TEST_ASSERT_EQUAL(ExtState::Disabled, t.state());
  // enabled but no measurement yet (e.g. right after a reboot): no write
  t.setEnabled(true, 2000);
  a = t.tick(3000, inputs(180, 200));
  TEST_ASSERT_FALSE(a.write);
  TEST_ASSERT_EQUAL(ExtState::WaitingSensor, t.state());
}

void test_heat_demand_boosts_above_stove_probe() {
  ExternalThermostat t;
  t.configure(cfg());
  t.onExternalTemp(185, 10000); // room cold: 18.5 < 20.0 - 0.5
  ExtAction a = t.tick(10000, inputs(215, 200)); // stove probe warm (21.5)
  TEST_ASSERT_TRUE(a.write);
  TEST_ASSERT_EQUAL_INT16(235, a.setpointTenths); // 21.5 + 2.0
  TEST_ASSERT_EQUAL(ExtDemand::Heat, t.demand());
  TEST_ASSERT_EQUAL(ExtState::Regulating, t.state());
  TEST_ASSERT_TRUE(t.baseKnown());
  TEST_ASSERT_EQUAL_INT16(200, t.baseTenths());
  // stove now at the desired setpoint: no further write
  a = t.tick(10001, inputs(215, 235));
  TEST_ASSERT_FALSE(a.write);
}

void test_idle_demand_relief_below_probe_and_hysteresis() {
  ExternalThermostat t;
  t.configure(cfg());
  t.onExternalTemp(206, 10000); // 20.6 >= 20.5 → idle
  ExtAction a = t.tick(10000, inputs(190, 230)); // stove probe 19.0
  TEST_ASSERT_TRUE(a.write);
  TEST_ASSERT_EQUAL_INT16(180, a.setpointTenths); // 19.0 - 1.0
  TEST_ASSERT_EQUAL(ExtDemand::Idle, t.demand());
  // inside the band the demand does not flip
  t.onExternalTemp(198, 400000);
  a = t.tick(400000, inputs(190, 180));
  TEST_ASSERT_EQUAL(ExtDemand::Idle, t.demand());
  TEST_ASSERT_FALSE(a.write);
  // below the band → heat
  t.onExternalTemp(194, 700000);
  a = t.tick(700000, inputs(190, 180));
  TEST_ASSERT_EQUAL(ExtDemand::Heat, t.demand());
  TEST_ASSERT_TRUE(a.write);
  TEST_ASSERT_EQUAL_INT16(210, a.setpointTenths); // 19.0 + 2.0
}

void test_rate_limit_and_clamps() {
  ExternalThermostat t;
  t.configure(cfg());
  t.onExternalTemp(150, 1000);
  ExtAction a = t.tick(1000, inputs(290, 200)); // probe 29.0 + 2.0 = 31 → clamp
  TEST_ASSERT_TRUE(a.write);
  TEST_ASSERT_EQUAL_INT16(240, a.setpointTenths); // target + maxOffset (4.0)
  // a second write within the interval is held back even if desired changes
  t.onExternalTemp(150, 2000);
  a = t.tick(2000, inputs(220, 240));
  TEST_ASSERT_FALSE(a.write);
  // after the interval it goes through, rounded to the 0.5 step
  a = t.tick(1000 + 300000, inputs(203, 240));
  TEST_ASSERT_TRUE(a.write);
  TEST_ASSERT_EQUAL_INT16(225, a.setpointTenths); // 20.3 + 2.0 = 22.3 → 22.5
  ExternalThermostat u;
  ExtThermostatConfig c = cfg();
  c.setpointTenths = 160;
  u.configure(c);
  u.onExternalTemp(170, 5000); // idle
  a = u.tick(5000, inputs(155, 200)); // 15.5 - 1.0 = 14.5 → profile min 15.0
  TEST_ASSERT_TRUE(a.write);
  TEST_ASSERT_EQUAL_INT16(150, a.setpointTenths);
}

void test_sensor_lost_restores_base_once_then_waits() {
  ExternalThermostat t;
  t.configure(cfg());
  t.onExternalTemp(180, 1000);
  ExtAction a = t.tick(1000, inputs(200, 195)); // base = 19.5
  TEST_ASSERT_TRUE(a.write);
  TEST_ASSERT_EQUAL_INT16(220, a.setpointTenths);
  // sensor silent for more than 10 min
  a = t.tick(1000 + 600001, inputs(200, 220));
  TEST_ASSERT_TRUE(a.write);
  TEST_ASSERT_EQUAL_INT16(195, a.setpointTenths);
  TEST_ASSERT_NOT_NULL(strstr(a.reason, "sensor lost"));
  TEST_ASSERT_EQUAL(ExtState::SensorLost, t.state());
  // no further writes while lost
  a = t.tick(1000 + 900000, inputs(200, 195));
  TEST_ASSERT_FALSE(a.write);
  a = t.tick(1000 + 1200000, inputs(200, 195));
  TEST_ASSERT_FALSE(a.write);
  // sensor back → regulates again (rate limit counted from the restore)
  t.onExternalTemp(180, 1000 + 1300000);
  a = t.tick(1000 + 1300000, inputs(200, 195));
  TEST_ASSERT_TRUE(a.write);
  TEST_ASSERT_EQUAL(ExtState::Regulating, t.state());
}

void test_disable_restores_base_and_never_touches_onoff() {
  ExternalThermostat t;
  t.configure(cfg());
  t.onExternalTemp(150, 1000);
  ExtAction a = t.tick(1000, inputs(200, 190));
  TEST_ASSERT_TRUE(a.write);
  t.setEnabled(false, 2000);
  a = t.tick(2000, inputs(200, 220));
  TEST_ASSERT_TRUE(a.write);
  TEST_ASSERT_EQUAL_INT16(190, a.setpointTenths);
  TEST_ASSERT_NOT_NULL(strstr(a.reason, "disabled"));
  a = t.tick(3000, inputs(200, 190));
  TEST_ASSERT_FALSE(a.write);
  TEST_ASSERT_EQUAL(ExtState::Disabled, t.state());
  // enabling again without any write then disabling: nothing to restore
  t.setEnabled(true, 4000);
  t.setEnabled(false, 5000);
  a = t.tick(5000, inputs(200, 190));
  TEST_ASSERT_FALSE(a.write);
}

void test_unknown_probe_uses_target() {
  ExternalThermostat t;
  t.configure(cfg());
  t.onExternalTemp(170, 1000);
  ExtInputs i;
  i.stoveSetpointKnown = true;
  i.stoveSetpointTenths = 200;
  ExtAction a = t.tick(1000, i);
  TEST_ASSERT_TRUE(a.write);
  TEST_ASSERT_EQUAL_INT16(220, a.setpointTenths); // target + boost
  TEST_ASSERT_EQUAL_STRING("heat", extDemandName(t.demand()));
  TEST_ASSERT_EQUAL_STRING("regulating", extStateName(t.state()));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_disabled_and_waiting_do_nothing);
  RUN_TEST(test_heat_demand_boosts_above_stove_probe);
  RUN_TEST(test_idle_demand_relief_below_probe_and_hysteresis);
  RUN_TEST(test_rate_limit_and_clamps);
  RUN_TEST(test_sensor_lost_restores_base_once_then_waits);
  RUN_TEST(test_disable_restores_base_and_never_touches_onoff);
  RUN_TEST(test_unknown_probe_uses_target);
  return UNITY_END();
}
