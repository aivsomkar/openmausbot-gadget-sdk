/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_input.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_button_debounces_bounce(void) {
  pl_button_t b;
  pl_button_init(&b, false);
  TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, true));  /* 1st pressed sample */
  TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, false)); /* bounce */
  TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, true));
  TEST_ASSERT_EQUAL_INT(1, pl_button_poll(&b, true));  /* stable for 2 polls */
  TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, true));
  TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, false));
  TEST_ASSERT_EQUAL_INT(-1, pl_button_poll(&b, false));
}

static void test_button_held_at_boot_is_ignored_until_released(void) {
  pl_button_t b;
  pl_button_init(&b, true);
  for (int i = 0; i < 300; i++) {
    TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, true));
  }
  TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, false));
  TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, false)); /* armed, no edge */
  TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, true));
  TEST_ASSERT_EQUAL_INT(1, pl_button_poll(&b, true));
}

static void test_touch_down_move_up(void) {
  pl_touch_t t;
  gadget_input_t ev;
  pl_touch_init(&t);
  TEST_ASSERT_EQUAL_INT(0, pl_touch_poll(&t, false, 0, 0, 466, 466, &ev));
  TEST_ASSERT_EQUAL_INT(1, pl_touch_poll(&t, true, 100, 200, 466, 466, &ev));
  TEST_ASSERT_EQUAL(GADGET_IN_TOUCH_DOWN, ev.type);
  TEST_ASSERT_EQUAL_INT16(200, ev.y);
  TEST_ASSERT_EQUAL_INT(0, pl_touch_poll(&t, true, 101, 200, 466, 466, &ev)); /* 1 px jitter */
  TEST_ASSERT_EQUAL_INT(1, pl_touch_poll(&t, true, 101, 230, 466, 466, &ev));
  TEST_ASSERT_EQUAL(GADGET_IN_TOUCH_MOVE, ev.type);
  TEST_ASSERT_EQUAL_INT(1, pl_touch_poll(&t, false, 0, 0, 466, 466, &ev));
  TEST_ASSERT_EQUAL(GADGET_IN_TOUCH_UP, ev.type);
  TEST_ASSERT_EQUAL_INT16(101, ev.x);
  TEST_ASSERT_EQUAL_INT16(230, ev.y);
}

static void test_touch_clamps_to_screen(void) {
  pl_touch_t t;
  gadget_input_t ev;
  pl_touch_init(&t);
  TEST_ASSERT_EQUAL_INT(1, pl_touch_poll(&t, true, 470, -3, 466, 466, &ev));
  TEST_ASSERT_EQUAL_INT16(465, ev.x);
  TEST_ASSERT_EQUAL_INT16(0, ev.y);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_button_debounces_bounce);
  RUN_TEST(test_button_held_at_boot_is_ignored_until_released);
  RUN_TEST(test_touch_down_move_up);
  RUN_TEST(test_touch_clamps_to_screen);
  return UNITY_END();
}
