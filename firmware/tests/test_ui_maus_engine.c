/* SPDX-License-Identifier: Apache-2.0 */
/* The Maus animation engine: blink-cut expression changes, deterministic
 * PRNG, integer motion, speaking mouths. Uses a hand-made art table. */
#include <string.h>

#include "ui_maus_engine.h"
#include "unity.h"

/* expr_ids index:  0  1  2  3 */
static const uint8_t k_ids[MAUS_EXPR_COUNT] = {6, 0, 8, 19};
static const uint8_t k_pool_idle[] = {0, 1, 2};
static const uint8_t k_pool_one[] = {2};
static const uint8_t k_pool_speak[] = {3, 0};
static const uint8_t k_speak_expr[MAUS_SPEAK_EXPRS] = {3, 0};
static maus_state_def_t k_states[UI_MAUS__COUNT];
static maus_art_t k_art;
static maus_engine_t e;

void setUp(void) {
  memset(k_states, 0, sizeof k_states);
  k_states[UI_MAUS_IDLE] = (maus_state_def_t){k_pool_idle, 3, 2000, 4000, 1000, 2000, 0, 0, 0, 0, 0, 0};
  k_states[UI_MAUS_SLEEPING] = (maus_state_def_t){k_pool_idle, 3, 2000, 4000, 0, 0, 0, 0, 0, 0, 0, 0};
  k_states[UI_MAUS_CURIOUS] = (maus_state_def_t){k_pool_one, 1, 1000, 1000, 0, 0, 100, 1000, 0, 0, 0, 0};
  k_states[UI_MAUS_ALERTING] = (maus_state_def_t){k_pool_one, 1, 1000, 1000, 0, 0, 0, 0, 27, 85, 0, 0};
  k_states[UI_MAUS_SPEAKING] = (maus_state_def_t){k_pool_speak, 2, 3000, 6000, 2500, 5000, 0, 0, 0, 0, 0, 0};
  memset(&k_art, 0, sizeof k_art);
  k_art.expr_ids = k_ids;
  k_art.speak_expr = k_speak_expr;
  k_art.states = k_states;
  maus_engine_init(&e, &k_art, 1);
}
void tearDown(void) {}

static void test_sine_table_hits_the_quarter_points(void) {
  TEST_ASSERT_EQUAL_INT32(0, maus_sin_q15(0));
  TEST_ASSERT_EQUAL_INT32(32767, maus_sin_q15(16384));
  TEST_ASSERT_EQUAL_INT32(0, maus_sin_q15(32768));
  TEST_ASSERT_EQUAL_INT32(-32767, maus_sin_q15(49152));
  TEST_ASSERT_INT32_WITHIN(40, 23170, maus_sin_q15(8192)); /* sin(pi/4) */
}

static void test_blink_closes_fast_and_opens_slowly(void) {
  TEST_ASSERT_EQUAL_UINT8(0, maus_blink_step(0));
  TEST_ASSERT_EQUAL_UINT8(1, maus_blink_step(40));
  TEST_ASSERT_EQUAL_UINT8(2, maus_blink_step(90));
  TEST_ASSERT_EQUAL_UINT8(3, maus_blink_step(134));
  TEST_ASSERT_EQUAL_UINT8(3, maus_blink_step(150));
  TEST_ASSERT_EQUAL_UINT8(2, maus_blink_step(200));
  TEST_ASSERT_EQUAL_UINT8(1, maus_blink_step(250));
  TEST_ASSERT_EQUAL_UINT8(0, maus_blink_step(319));
}

static void test_first_state_shows_its_first_expression_at_once(void) {
  maus_engine_set_state(&e, UI_MAUS_IDLE, 0);
  maus_frame_t f = maus_engine_step(&e, 0, 0);
  TEST_ASSERT_EQUAL_UINT8(0, f.expr);
  TEST_ASSERT_EQUAL_UINT8(0, f.blink_step);
}

static void test_expression_changes_only_while_the_eyes_are_closed(void) {
  maus_engine_set_state(&e, UI_MAUS_IDLE, 0);
  uint8_t last = maus_engine_step(&e, 0, 0).expr;
  int changes = 0;
  for (uint64_t t = 10; t <= 60000; t += 10) {
    maus_frame_t f = maus_engine_step(&e, t, 0);
    if (f.expr != last) {
      TEST_ASSERT_EQUAL_UINT8(MAUS_BLINK_STEPS - 1, f.blink_step);
      changes++;
      last = f.expr;
    }
  }
  TEST_ASSERT_GREATER_OR_EQUAL(10, changes); /* cadence 2-4 s over 60 s */
}

static void test_state_change_swaps_expression_under_a_blink(void) {
  maus_engine_set_state(&e, UI_MAUS_IDLE, 0);
  (void)maus_engine_step(&e, 0, 0);
  maus_engine_set_state(&e, UI_MAUS_CURIOUS, 500); /* pool {2}, current is 0 */
  maus_frame_t f = maus_engine_step(&e, 500, 0);
  TEST_ASSERT_EQUAL_UINT8(0, f.expr); /* not yet: eyes still open */
  for (uint64_t t = 510; t <= 900; t += 10) {
    f = maus_engine_step(&e, t, 0);
    if (f.expr == 2) {
      TEST_ASSERT_EQUAL_UINT8(MAUS_BLINK_STEPS - 1, f.blink_step);
      return;
    }
  }
  TEST_FAIL_MESSAGE("curious never reached its first expression");
}

static void test_never_blinking_states_only_blink_to_change_expression(void) {
  maus_engine_set_state(&e, UI_MAUS_SLEEPING, 0); /* blink 0, 0: never on its own */
  maus_frame_t prev = maus_engine_step(&e, 0, 0);
  int blinks = 0;
  int changes = 0;
  for (uint64_t t = 10; t <= 30000; t += 10) {
    maus_frame_t f = maus_engine_step(&e, t, 0);
    if (prev.blink_step == 0 && f.blink_step != 0) blinks++;
    if (f.expr != prev.expr) changes++;
    prev = f;
  }
  TEST_ASSERT_GREATER_THAN(0, changes);
  /* every blink carries an expression change (the last one may still be closing) */
  TEST_ASSERT_TRUE(blinks == changes || blinks == changes + 1);
}

static void test_same_seed_same_frames(void) {
  maus_engine_t a, b;
  maus_engine_init(&a, &k_art, 7);
  maus_engine_init(&b, &k_art, 7);
  maus_engine_set_state(&a, UI_MAUS_IDLE, 0);
  maus_engine_set_state(&b, UI_MAUS_IDLE, 0);
  for (uint64_t t = 0; t <= 30000; t += 10) {
    maus_frame_t fa = maus_engine_step(&a, t, 0);
    maus_frame_t fb = maus_engine_step(&b, t, 0);
    TEST_ASSERT_EQUAL_MEMORY(&fa, &fb, sizeof fa);
  }
}

static void test_different_seeds_differ(void) {
  maus_engine_t a, b;
  maus_engine_init(&a, &k_art, 1);
  maus_engine_init(&b, &k_art, 2);
  maus_engine_set_state(&a, UI_MAUS_IDLE, 0);
  maus_engine_set_state(&b, UI_MAUS_IDLE, 0);
  int differ = 0;
  for (uint64_t t = 0; t <= 30000; t += 10) {
    maus_frame_t fa = maus_engine_step(&a, t, 0);
    maus_frame_t fb = maus_engine_step(&b, t, 0);
    if (fa.expr != fb.expr || fa.blink_step != fb.blink_step) differ++;
  }
  TEST_ASSERT_GREATER_THAN(0, differ);
}

static void test_bob_moves_up_then_down_in_whole_pixels(void) {
  maus_engine_set_state(&e, UI_MAUS_CURIOUS, 0); /* bob 10.0 px, 1000 ms */
  TEST_ASSERT_EQUAL_INT16(0, maus_engine_step(&e, 0, 0).dy);
  TEST_ASSERT_EQUAL_INT16(-10, maus_engine_step(&e, 250, 0).dy);
  TEST_ASSERT_EQUAL_INT16(0, maus_engine_step(&e, 500, 0).dy);
  TEST_ASSERT_EQUAL_INT16(10, maus_engine_step(&e, 750, 0).dy);
  TEST_ASSERT_EQUAL_INT16(0, maus_engine_step(&e, 750, 0).dx);
}

static void test_jitter_stays_within_its_amplitude(void) {
  maus_engine_set_state(&e, UI_MAUS_ALERTING, 0); /* jitter 2.7 px, 85 ms */
  int moved = 0;
  for (uint64_t t = 0; t <= 2000; t += 10) {
    maus_frame_t f = maus_engine_step(&e, t, 0);
    TEST_ASSERT_TRUE(f.dx >= -3 && f.dx <= 3);
    TEST_ASSERT_TRUE(f.dy >= -3 && f.dy <= 3);
    if (f.dx != 0 || f.dy != 0) moved++;
  }
  TEST_ASSERT_GREATER_THAN(50, moved);
}

static void test_speaking_opens_the_mouth_only_when_speaking(void) {
  maus_engine_set_state(&e, UI_MAUS_SPEAKING, 0); /* first expression: index 3 (app 19) */
  maus_frame_t f = maus_engine_step(&e, 0, 2);
  TEST_ASSERT_EQUAL_INT8(0, f.speak); /* speak_expr[0] == 3 */
  TEST_ASSERT_EQUAL_UINT8(2, f.speak_level);
  f = maus_engine_step(&e, 10, 0);
  TEST_ASSERT_EQUAL_INT8(-1, f.speak);
  f = maus_engine_step(&e, 20, 9); /* clamped to the art's levels */
  TEST_ASSERT_EQUAL_UINT8(MAUS_SPEAK_LEVELS, f.speak_level);
  maus_engine_set_state(&e, UI_MAUS_IDLE, 30);
  f = maus_engine_step(&e, 30, 3);
  TEST_ASSERT_EQUAL_INT8(-1, f.speak);
}

static void test_hidden_state_draws_nothing(void) {
  maus_frame_t f = maus_engine_step(&e, 100, 3);
  TEST_ASSERT_EQUAL_INT8(-1, f.speak);
  TEST_ASSERT_EQUAL_INT16(0, f.dx);
  TEST_ASSERT_EQUAL_INT16(0, f.dy);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_sine_table_hits_the_quarter_points);
  RUN_TEST(test_blink_closes_fast_and_opens_slowly);
  RUN_TEST(test_first_state_shows_its_first_expression_at_once);
  RUN_TEST(test_expression_changes_only_while_the_eyes_are_closed);
  RUN_TEST(test_state_change_swaps_expression_under_a_blink);
  RUN_TEST(test_never_blinking_states_only_blink_to_change_expression);
  RUN_TEST(test_same_seed_same_frames);
  RUN_TEST(test_different_seeds_differ);
  RUN_TEST(test_bob_moves_up_then_down_in_whole_pixels);
  RUN_TEST(test_jitter_stays_within_its_amplitude);
  RUN_TEST(test_speaking_opens_the_mouth_only_when_speaking);
  RUN_TEST(test_hidden_state_draws_nothing);
  return UNITY_END();
}
