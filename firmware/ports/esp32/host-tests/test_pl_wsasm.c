/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include "pl_wsasm.h"
#include "unity.h"

static uint8_t buf[GADGET_TEXT_FRAME_MAX];
static pl_wsasm_t a;
static const uint8_t *msg;
static size_t msg_len;

void setUp(void) {
  pl_wsasm_init(&a, buf, GADGET_TEXT_FRAME_MAX, GADGET_BINARY_FRAME_MAX);
  msg = NULL;
  msg_len = 0;
}
void tearDown(void) {}

static pl_ws_out_t feed(uint8_t op, bool fin, size_t plen, size_t off, const char *s) {
  return pl_wsasm_feed(&a, op, fin, plen, off, (const uint8_t *)s, strlen(s), &msg, &msg_len);
}

static void test_single_piece_text(void) {
  TEST_ASSERT_EQUAL(PL_WS_TEXT, feed(PL_WS_OP_TEXT, true, 12, 0, "{\"op\":\"x\"}ab"));
  TEST_ASSERT_EQUAL_size_t(12, msg_len);
}

static void test_frame_split_across_events(void) {
  TEST_ASSERT_EQUAL(PL_WS_NONE, feed(PL_WS_OP_TEXT, true, 10, 0, "hello"));
  TEST_ASSERT_EQUAL(PL_WS_TEXT, feed(PL_WS_OP_TEXT, true, 10, 5, "world"));
  TEST_ASSERT_EQUAL_size_t(10, msg_len);
  TEST_ASSERT_EQUAL_MEMORY("helloworld", msg, 10);
}

static void test_fragmented_message_with_ping_between(void) {
  TEST_ASSERT_EQUAL(PL_WS_NONE, feed(PL_WS_OP_BINARY, false, 3, 0, "\x02\x01""a"));
  TEST_ASSERT_EQUAL(PL_WS_CONTROL, feed(PL_WS_OP_PING, true, 0, 0, ""));
  TEST_ASSERT_EQUAL(PL_WS_NONE, feed(PL_WS_OP_CONT, false, 2, 0, "bc"));
  TEST_ASSERT_EQUAL(PL_WS_BINARY, feed(PL_WS_OP_CONT, true, 2, 0, "de"));
  TEST_ASSERT_EQUAL_size_t(7, msg_len);
  TEST_ASSERT_EQUAL_MEMORY("\x02\x01""abcde", msg, 7);
}

static void test_empty_text_message(void) {
  TEST_ASSERT_EQUAL(PL_WS_TEXT, pl_wsasm_feed(&a, PL_WS_OP_TEXT, true, 0, 0, NULL, 0, &msg, &msg_len));
  TEST_ASSERT_EQUAL_size_t(0, msg_len);
}

static void test_binary_over_8k_is_too_big_and_sticks(void) {
  static uint8_t big[GADGET_BINARY_FRAME_MAX + 1];
  TEST_ASSERT_EQUAL(PL_WS_TOO_BIG,
                    pl_wsasm_feed(&a, PL_WS_OP_BINARY, true, sizeof(big), 0, big, sizeof(big), &msg, &msg_len));
  TEST_ASSERT_EQUAL(PL_WS_NONE, feed(PL_WS_OP_TEXT, true, 2, 0, "ok"));
  pl_wsasm_reset(&a);
  TEST_ASSERT_EQUAL(PL_WS_TEXT, feed(PL_WS_OP_TEXT, true, 2, 0, "ok"));
}

static void test_fragments_over_16k_are_too_big(void) {
  static uint8_t half[GADGET_TEXT_FRAME_MAX / 2];
  TEST_ASSERT_EQUAL(PL_WS_NONE,
                    pl_wsasm_feed(&a, PL_WS_OP_TEXT, false, sizeof(half), 0, half, sizeof(half), &msg, &msg_len));
  TEST_ASSERT_EQUAL(PL_WS_NONE,
                    pl_wsasm_feed(&a, PL_WS_OP_CONT, false, sizeof(half), 0, half, sizeof(half), &msg, &msg_len));
  TEST_ASSERT_EQUAL(PL_WS_TOO_BIG, feed(PL_WS_OP_CONT, true, 1, 0, "x"));
}

static void test_text_of_exactly_16k_is_accepted(void) {
  static uint8_t big[GADGET_TEXT_FRAME_MAX];
  memset(big, 'a', sizeof(big));
  TEST_ASSERT_EQUAL(PL_WS_NONE, pl_wsasm_feed(&a, PL_WS_OP_TEXT, true, sizeof(big), 0, big, 8000, &msg, &msg_len));
  TEST_ASSERT_EQUAL(PL_WS_TEXT, pl_wsasm_feed(&a, PL_WS_OP_TEXT, true, sizeof(big), 8000, big + 8000,
                                              sizeof(big) - 8000, &msg, &msg_len));
  TEST_ASSERT_EQUAL_size_t(GADGET_TEXT_FRAME_MAX, msg_len);
}

static void test_protocol_errors(void) {
  TEST_ASSERT_EQUAL(PL_WS_PROTOCOL, feed(PL_WS_OP_CONT, true, 1, 0, "x"));
  pl_wsasm_reset(&a);
  TEST_ASSERT_EQUAL(PL_WS_NONE, feed(PL_WS_OP_TEXT, false, 1, 0, "x"));
  TEST_ASSERT_EQUAL(PL_WS_PROTOCOL, feed(PL_WS_OP_TEXT, true, 1, 0, "y"));
  pl_wsasm_reset(&a);
  TEST_ASSERT_EQUAL(PL_WS_PROTOCOL, feed(0x3, true, 1, 0, "z"));
}

static void test_close_frame_is_ignored(void) {
  TEST_ASSERT_EQUAL(PL_WS_NONE, feed(PL_WS_OP_CLOSE, true, 2, 0, "\x03\xe8"));
  TEST_ASSERT_EQUAL(PL_WS_TEXT, feed(PL_WS_OP_TEXT, true, 1, 0, "k"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_single_piece_text);
  RUN_TEST(test_frame_split_across_events);
  RUN_TEST(test_fragmented_message_with_ping_between);
  RUN_TEST(test_empty_text_message);
  RUN_TEST(test_binary_over_8k_is_too_big_and_sticks);
  RUN_TEST(test_fragments_over_16k_are_too_big);
  RUN_TEST(test_text_of_exactly_16k_is_accepted);
  RUN_TEST(test_protocol_errors);
  RUN_TEST(test_close_frame_is_ignored);
  return UNITY_END();
}
