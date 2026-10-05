/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_power.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_axp2101_decode(void) {
  gadget_battery_t b;
  TEST_ASSERT_FALSE(pl_axp2101_decode(0x00, 0x00, 55, &b)); /* no battery */
  TEST_ASSERT_TRUE(pl_axp2101_decode(0x08, 0x20, 82, &b)); /* bits6:5 = 01 */
  TEST_ASSERT_EQUAL_UINT8(82, b.pct);
  TEST_ASSERT_TRUE(b.charging);
  TEST_ASSERT_TRUE(pl_axp2101_decode(0x28, 0x40, 140, &b)); /* discharging, bogus pct */
  TEST_ASSERT_EQUAL_UINT8(100, b.pct);
  TEST_ASSERT_FALSE(b.charging);
  TEST_ASSERT_TRUE(pl_axp2101_decode(0x08, 0x04, 100, &b)); /* standby, charge done */
  TEST_ASSERT_FALSE(b.charging);
}

static void test_lipo_curve(void) {
  TEST_ASSERT_EQUAL_UINT8(100, pl_lipo_pct(4250));
  TEST_ASSERT_EQUAL_UINT8(100, pl_lipo_pct(4180));
  TEST_ASSERT_EQUAL_UINT8(50, pl_lipo_pct(3830));
  TEST_ASSERT_EQUAL_UINT8(45, pl_lipo_pct(3810));
  TEST_ASSERT_EQUAL_UINT8(0, pl_lipo_pct(3300));
  TEST_ASSERT_EQUAL_UINT8(0, pl_lipo_pct(2900));
  uint8_t last = 0;
  for (uint32_t mv = 3300; mv <= 4180; mv += 10) { /* never decreases with voltage */
    uint8_t p = pl_lipo_pct(mv);
    TEST_ASSERT_TRUE(p >= last);
    last = p;
  }
}

/* lcd-154 without a cell: the divider sits near ground, so report no battery
 * (hal_battery_read() false) instead of a made-up 0 %. */
static void test_lipo_no_cell(void) {
  TEST_ASSERT_FALSE(pl_lipo_present(0));
  TEST_ASSERT_FALSE(pl_lipo_present(PL_LIPO_NO_CELL_MV - 1));
  TEST_ASSERT_TRUE(pl_lipo_present(PL_LIPO_NO_CELL_MV));
  TEST_ASSERT_TRUE(pl_lipo_present(3300));
  TEST_ASSERT_TRUE(pl_lipo_present(4200));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_axp2101_decode);
  RUN_TEST(test_lipo_curve);
  RUN_TEST(test_lipo_no_cell);
  return UNITY_END();
}
