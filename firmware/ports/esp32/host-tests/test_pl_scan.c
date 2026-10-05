/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <string.h>

#include "pl_scan.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static gadget_wifi_ap_t ap(const char *ssid, int8_t rssi, gadget_wifi_auth_t auth) {
  gadget_wifi_ap_t a;
  memset(&a, 0, sizeof(a));
  snprintf(a.ssid, sizeof(a.ssid), "%s", ssid);
  a.rssi = rssi;
  a.auth = auth;
  return a;
}

static void test_dedupes_keeps_strongest_and_sorts(void) {
  gadget_wifi_ap_t in[] = {
    ap("Cafe", -80, GADGET_AUTH_OPEN), ap("Home", -60, GADGET_AUTH_WPA2), ap("", -30, GADGET_AUTH_WPA2),
    ap("Home", -42, GADGET_AUTH_WPA3), ap("Office", -42, GADGET_AUTH_WPA2_ENT), ap("Cafe", -90, GADGET_AUTH_OPEN),
  };
  gadget_wifi_ap_t out[PL_SCAN_MAX];
  uint8_t n = pl_scan_merge(in, sizeof(in) / sizeof(in[0]), out, PL_SCAN_MAX);
  TEST_ASSERT_EQUAL_UINT8(3, n);
  TEST_ASSERT_EQUAL_STRING("Home", out[0].ssid); /* -42, "Home" < "Office" */
  TEST_ASSERT_EQUAL_INT8(-42, out[0].rssi);
  TEST_ASSERT_EQUAL(GADGET_AUTH_WPA3, out[0].auth);
  TEST_ASSERT_EQUAL_STRING("Office", out[1].ssid);
  TEST_ASSERT_EQUAL_STRING("Cafe", out[2].ssid);
  TEST_ASSERT_EQUAL_INT8(-80, out[2].rssi);
}

static void test_caps_at_twenty_strongest(void) {
  gadget_wifi_ap_t in[30];
  for (int i = 0; i < 30; i++) {
    char name[8];
    snprintf(name, sizeof(name), "n%02d", i);
    in[i] = ap(name, (int8_t)(-90 + i), GADGET_AUTH_WPA2); /* n29 strongest */
  }
  gadget_wifi_ap_t out[PL_SCAN_MAX];
  uint8_t n = pl_scan_merge(in, 30, out, 200);
  TEST_ASSERT_EQUAL_UINT8(PL_SCAN_MAX, n);
  TEST_ASSERT_EQUAL_STRING("n29", out[0].ssid);
  TEST_ASSERT_EQUAL_STRING("n10", out[19].ssid);
}

static void test_late_stronger_duplicate_of_evicted_ssid(void) {
  gadget_wifi_ap_t in[4] = {ap("A", -50, 0), ap("B", -60, 0), ap("C", -70, 0), ap("C", -40, 0)};
  gadget_wifi_ap_t out[2];
  uint8_t n = pl_scan_merge(in, 4, out, 2);
  TEST_ASSERT_EQUAL_UINT8(2, n);
  TEST_ASSERT_EQUAL_STRING("C", out[0].ssid);
  TEST_ASSERT_EQUAL_STRING("A", out[1].ssid);
}

static void test_unterminated_ssid_is_cut(void) {
  gadget_wifi_ap_t in[1];
  memset(&in[0], 'x', sizeof(in[0].ssid));
  in[0].rssi = -50;
  in[0].auth = GADGET_AUTH_OPEN;
  gadget_wifi_ap_t out[1];
  TEST_ASSERT_EQUAL_UINT8(1, pl_scan_merge(in, 1, out, 1));
  TEST_ASSERT_EQUAL_size_t(32, strlen(out[0].ssid));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_dedupes_keeps_strongest_and_sorts);
  RUN_TEST(test_caps_at_twenty_strongest);
  RUN_TEST(test_late_stronger_duplicate_of_evicted_ssid);
  RUN_TEST(test_unterminated_ssid_is_cut);
  return UNITY_END();
}
