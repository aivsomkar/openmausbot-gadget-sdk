/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include "pl_mdns.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const char *ID = "000102030405060708090a0b0c0d0e0f";

static void test_full_answer(void) {
  pl_mdns_in_t in = {"Omkar's computer", "omkar-mac", 8810, true, {192, 168, 1, 20}, ID, 32};
  gadget_mdns_host_t h;
  TEST_ASSERT_TRUE(pl_mdns_host(&in, &h));
  TEST_ASSERT_EQUAL_STRING("Omkar's computer", h.name);
  TEST_ASSERT_EQUAL_STRING("192.168.1.20:8810", h.address);
  TEST_ASSERT_EQUAL_STRING(ID, h.id);
}

static void test_bad_or_missing_id_is_empty(void) {
  gadget_mdns_host_t h;
  pl_mdns_in_t in = {"x", NULL, 8810, true, {10, 0, 0, 2}, "000102030405060708090A0B0C0D0E0F", 32};
  TEST_ASSERT_TRUE(pl_mdns_host(&in, &h));
  TEST_ASSERT_EQUAL_STRING("", h.id); /* uppercase is not the canonical form */
  in.txt_id = ID;
  in.txt_id_len = 31;
  TEST_ASSERT_TRUE(pl_mdns_host(&in, &h));
  TEST_ASSERT_EQUAL_STRING("", h.id);
  in.txt_id = NULL;
  in.txt_id_len = 0;
  TEST_ASSERT_TRUE(pl_mdns_host(&in, &h));
  TEST_ASSERT_EQUAL_STRING("", h.id);
}

static void test_name_fallbacks_and_cut(void) {
  gadget_mdns_host_t h;
  pl_mdns_in_t in = {NULL, "omkar-mac", 8810, true, {10, 0, 0, 2}, NULL, 0};
  TEST_ASSERT_TRUE(pl_mdns_host(&in, &h));
  TEST_ASSERT_EQUAL_STRING("omkar-mac", h.name);
  in.hostname = "";
  TEST_ASSERT_TRUE(pl_mdns_host(&in, &h));
  TEST_ASSERT_EQUAL_STRING("MausBot", h.name);
  char longname[100];
  memset(longname, 'n', 99);
  longname[99] = '\0';
  in.instance = longname;
  TEST_ASSERT_TRUE(pl_mdns_host(&in, &h));
  TEST_ASSERT_EQUAL_size_t(63, strlen(h.name));
}

static void test_unusable_answers(void) {
  gadget_mdns_host_t h;
  pl_mdns_in_t in = {"x", NULL, 8810, false, {0, 0, 0, 0}, NULL, 0};
  TEST_ASSERT_FALSE(pl_mdns_host(&in, &h));
  in.has_ipv4 = true;
  in.port = 0;
  TEST_ASSERT_FALSE(pl_mdns_host(&in, &h));
}

static void test_txt_key_and_list_dedupe(void) {
  TEST_ASSERT_TRUE(pl_mdns_txt_is_id("id"));
  TEST_ASSERT_TRUE(pl_mdns_txt_is_id("ID"));
  TEST_ASSERT_FALSE(pl_mdns_txt_is_id("idx"));
  TEST_ASSERT_FALSE(pl_mdns_txt_is_id("v"));
  gadget_mdns_host_t list[2], a = {"A", "10.0.0.1:8810", ""}, b = {"B", "10.0.0.2:8810", ""}, c = {"C", "10.0.0.3:8810", ""};
  uint8_t n = 0;
  n = pl_mdns_add(list, n, 2, &a);
  n = pl_mdns_add(list, n, 2, &a);
  TEST_ASSERT_EQUAL_UINT8(1, n);
  n = pl_mdns_add(list, n, 2, &b);
  n = pl_mdns_add(list, n, 2, &c);
  TEST_ASSERT_EQUAL_UINT8(2, n);
  TEST_ASSERT_EQUAL_STRING("B", list[1].name);
}

/* The whole browse fits in timeout_ms: core gives up 1 s after it. */
static void test_browse_budget(void) {
  TEST_ASSERT_EQUAL_UINT32(4000, pl_mdns_ptr_ms(5000)); /* GADGET_HOST_AUTO_TIMEOUT_MS */
  TEST_ASSERT_EQUAL_UINT32(1001, pl_mdns_ptr_ms(2001));
  TEST_ASSERT_EQUAL_UINT32(2000, pl_mdns_ptr_ms(2000));
  TEST_ASSERT_EQUAL_UINT32(500, pl_mdns_ptr_ms(500));
  TEST_ASSERT_EQUAL_UINT32(PL_MDNS_A_TIMEOUT_MS, pl_mdns_a_ms(10000, 15000));
  TEST_ASSERT_EQUAL_UINT32(500, pl_mdns_a_ms(14500, 15000));
  TEST_ASSERT_EQUAL_UINT32(201, pl_mdns_a_ms(14799, 15000));
  TEST_ASSERT_EQUAL_UINT32(0, pl_mdns_a_ms(14800, 15000)); /* under 200 ms left: no lookup */
  TEST_ASSERT_EQUAL_UINT32(0, pl_mdns_a_ms(16000, 15000));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_full_answer);
  RUN_TEST(test_bad_or_missing_id_is_empty);
  RUN_TEST(test_name_fallbacks_and_cut);
  RUN_TEST(test_unusable_answers);
  RUN_TEST(test_txt_key_and_list_dedupe);
  RUN_TEST(test_browse_budget);
  return UNITY_END();
}
