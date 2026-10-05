/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_display.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_round_even_area(void) {
  int32_t x1 = 3, y1 = 4, x2 = 10, y2 = 465;
  pl_round_even_area(&x1, &y1, &x2, &y2, 466, 466);
  TEST_ASSERT_EQUAL_INT32(2, x1);
  TEST_ASSERT_EQUAL_INT32(4, y1);
  TEST_ASSERT_EQUAL_INT32(11, x2);
  TEST_ASSERT_EQUAL_INT32(465, y2);
  x1 = 0; y1 = 0; x2 = 0; y2 = 0;
  pl_round_even_area(&x1, &y1, &x2, &y2, 466, 466);
  TEST_ASSERT_EQUAL_INT32(1, x2);
  TEST_ASSERT_EQUAL_INT32(1, y2);
}

static void test_area_already_aligned_is_unchanged(void) {
  int32_t x1 = 10, y1 = 20, x2 = 101, y2 = 41;
  pl_round_even_area(&x1, &y1, &x2, &y2, 240, 240);
  TEST_ASSERT_EQUAL_INT32(10, x1);
  TEST_ASSERT_EQUAL_INT32(20, y1);
  TEST_ASSERT_EQUAL_INT32(101, x2);
  TEST_ASSERT_EQUAL_INT32(41, y2);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_round_even_area);
  RUN_TEST(test_area_already_aligned_is_unchanged);
  return UNITY_END();
}
