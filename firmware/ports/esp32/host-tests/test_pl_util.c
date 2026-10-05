/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include "pl_util.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_storage_keys(void) {
  TEST_ASSERT_TRUE(pl_storage_key_ok("dev_key"));
  TEST_ASSERT_TRUE(pl_storage_key_ok("123456789012345"));
  TEST_ASSERT_FALSE(pl_storage_key_ok("1234567890123456"));
  TEST_ASSERT_FALSE(pl_storage_key_ok(""));
  TEST_ASSERT_FALSE(pl_storage_key_ok(NULL));
}

static void test_ws_uri(void) {
  char uri[300];
  TEST_ASSERT_EQUAL(GADGET_OK, pl_ws_uri(uri, sizeof(uri), "192.168.1.20", 8810));
  TEST_ASSERT_EQUAL_STRING("ws://192.168.1.20:8810/gadget", uri);
  TEST_ASSERT_EQUAL(GADGET_OK, pl_ws_uri(uri, sizeof(uri), "omkars-mac.local", 9000));
  TEST_ASSERT_EQUAL_STRING("ws://omkars-mac.local:9000/gadget", uri);
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_ws_uri(uri, sizeof(uri), "fe80::1", 8810));
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_ws_uri(uri, sizeof(uri), "a b", 8810));
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_ws_uri(uri, sizeof(uri), "host/evil", 8810));
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_ws_uri(uri, sizeof(uri), "", 8810));
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_ws_uri(uri, sizeof(uri), "10.0.0.1", 0));
  TEST_ASSERT_EQUAL(GADGET_ERR_LIMIT, pl_ws_uri(uri, 10, "10.0.0.1", 8810));
}

static void test_utf8_trunc(void) {
  char out[8];
  pl_utf8_trunc(out, sizeof(out), "abc");
  TEST_ASSERT_EQUAL_STRING("abc", out);
  pl_utf8_trunc(out, sizeof(out), "abcdefghij");
  TEST_ASSERT_EQUAL_STRING("abcdefg", out);
  /* "abcde" + U+00E9 (2 bytes) + "z": byte 7 would split the é */
  pl_utf8_trunc(out, 7, "abcde\xC3\xA9z");
  TEST_ASSERT_EQUAL_STRING("abcde", out);
  pl_utf8_trunc(out, 8, "abcde\xC3\xA9z");
  TEST_ASSERT_EQUAL_STRING("abcde\xC3\xA9", out);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_storage_keys);
  RUN_TEST(test_ws_uri);
  RUN_TEST(test_utf8_trunc);
  return UNITY_END();
}
