/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_wifi.h"
#include "unity.h"

static pl_wifi_t w;

void setUp(void) { pl_wifi_init(&w); }
void tearDown(void) {}

static void test_connect_posts_connecting_then_connected(void) {
  pl_wifi_act_t a = pl_wifi_connect(&w, 0);
  TEST_ASSERT_FALSE(a.disconnect);
  TEST_ASSERT_TRUE(a.connect);
  TEST_ASSERT_TRUE(a.post);
  TEST_ASSERT_EQUAL(GADGET_WIFI_CONNECTING, a.state);
  a = pl_wifi_got_ip(&w);
  TEST_ASSERT_TRUE(a.post);
  TEST_ASSERT_EQUAL(GADGET_WIFI_CONNECTED, a.state);
  TEST_ASSERT_EQUAL(GADGET_WIFI_CONNECTED, w.reported);
}

static void test_drop_after_connected_retries_at_once(void) {
  pl_wifi_connect(&w, 0);
  pl_wifi_got_ip(&w);
  pl_wifi_act_t a = pl_wifi_sta_disconnected(&w, false, false, 5000);
  TEST_ASSERT_TRUE(a.connect);
  TEST_ASSERT_TRUE(a.post);
  TEST_ASSERT_EQUAL(GADGET_WIFI_CONNECTING, a.state);
}

static void test_three_failures_report_failed_and_keep_retrying(void) {
  pl_wifi_connect(&w, 0);
  pl_wifi_act_t a = pl_wifi_sta_disconnected(&w, false, false, 100);
  TEST_ASSERT_FALSE(a.post);
  TEST_ASSERT_FALSE(a.connect);
  a = pl_wifi_tick(&w, 1099);
  TEST_ASSERT_FALSE(a.connect);
  a = pl_wifi_tick(&w, 1100);
  TEST_ASSERT_TRUE(a.connect);
  a = pl_wifi_sta_disconnected(&w, false, false, 1200);
  TEST_ASSERT_FALSE(a.post);
  a = pl_wifi_tick(&w, 3200);
  TEST_ASSERT_TRUE(a.connect);
  a = pl_wifi_sta_disconnected(&w, false, false, 3300);
  TEST_ASSERT_TRUE(a.post);
  TEST_ASSERT_EQUAL(GADGET_WIFI_FAILED, a.state);
  a = pl_wifi_tick(&w, 8300);
  TEST_ASSERT_TRUE(a.connect);
  a = pl_wifi_sta_disconnected(&w, false, false, 8400);
  TEST_ASSERT_FALSE(a.post); /* already FAILED: no repeat */
  TEST_ASSERT_EQUAL_UINT32(10000, pl_wifi_retry_delay_ms(4, false));
  a = pl_wifi_tick(&w, 18400);
  TEST_ASSERT_TRUE(a.connect);
  a = pl_wifi_got_ip(&w);
  TEST_ASSERT_EQUAL(GADGET_WIFI_CONNECTED, a.state);
}

static void test_wrong_password_fails_at_once_and_backs_off(void) {
  pl_wifi_connect(&w, 0);
  pl_wifi_act_t a = pl_wifi_sta_disconnected(&w, false, true, 100);
  TEST_ASSERT_TRUE(a.post);
  TEST_ASSERT_EQUAL(GADGET_WIFI_FAILED, a.state);
  TEST_ASSERT_FALSE(pl_wifi_tick(&w, 30099).connect);
  TEST_ASSERT_TRUE(pl_wifi_tick(&w, 30100).connect);
}

static void test_scan_while_connecting_aborts_attempt_then_resumes(void) {
  pl_wifi_connect(&w, 0);
  pl_wifi_act_t a = pl_wifi_scan(&w, 10);
  TEST_ASSERT_TRUE(a.disconnect);
  TEST_ASSERT_FALSE(a.scan);
  a = pl_wifi_sta_disconnected(&w, true, false, 20); /* our own disconnect */
  TEST_ASSERT_TRUE(a.scan);
  TEST_ASSERT_FALSE(a.post);
  TEST_ASSERT_FALSE(pl_wifi_tick(&w, 5000).connect); /* no retry while scanning */
  a = pl_wifi_scan_done(&w, 2000);
  TEST_ASSERT_TRUE(a.connect);
}

static void test_scan_pending_gives_up_waiting_after_three_seconds(void) {
  pl_wifi_connect(&w, 0);
  pl_wifi_scan(&w, 10);
  TEST_ASSERT_FALSE(pl_wifi_tick(&w, 3009).scan);
  TEST_ASSERT_TRUE(pl_wifi_tick(&w, 3010).scan);
}

static void test_scan_when_idle_or_connected_starts_at_once(void) {
  TEST_ASSERT_TRUE(pl_wifi_scan(&w, 0).scan);
  TEST_ASSERT_FALSE(pl_wifi_scan(&w, 1).scan); /* already scanning */
  pl_wifi_scan_done(&w, 2);
  TEST_ASSERT_FALSE(pl_wifi_tick(&w, 3).connect); /* not configured */
  pl_wifi_connect(&w, 10);
  pl_wifi_got_ip(&w);
  pl_wifi_act_t a = pl_wifi_scan(&w, 20);
  TEST_ASSERT_TRUE(a.scan);
  TEST_ASSERT_FALSE(a.disconnect);
}

static void test_attempt_timeout_counts_as_failure(void) {
  pl_wifi_connect(&w, 0);
  pl_wifi_act_t a = pl_wifi_tick(&w, PL_WIFI_ATTEMPT_TIMEOUT_MS);
  TEST_ASSERT_TRUE(a.disconnect);
  TEST_ASSERT_EQUAL_UINT8(1, w.failures);
  a = pl_wifi_sta_disconnected(&w, false, false, PL_WIFI_ATTEMPT_TIMEOUT_MS + 5);
  TEST_ASSERT_EQUAL_UINT8(1, w.failures); /* late report for the abandoned attempt */
}

static void test_disconnect_request_reports_off_and_stops(void) {
  pl_wifi_connect(&w, 0);
  pl_wifi_act_t a = pl_wifi_disconnect(&w);
  TEST_ASSERT_TRUE(a.disconnect);
  TEST_ASSERT_EQUAL(GADGET_WIFI_OFF, a.state);
  a = pl_wifi_sta_disconnected(&w, false, false, 100);
  TEST_ASSERT_FALSE(a.connect);
  TEST_ASSERT_FALSE(pl_wifi_tick(&w, 60000).connect);
  TEST_ASSERT_FALSE(pl_wifi_got_ip(&w).post);
}

static void test_new_network_while_connected_disconnects_first(void) {
  pl_wifi_connect(&w, 0);
  pl_wifi_got_ip(&w);
  pl_wifi_act_t a = pl_wifi_connect(&w, 100);
  TEST_ASSERT_TRUE(a.disconnect);
  TEST_ASSERT_TRUE(a.connect);
  TEST_ASSERT_EQUAL(GADGET_WIFI_CONNECTING, a.state);
  a = pl_wifi_sta_disconnected(&w, true, false, 110); /* our disconnect is not a failure */
  TEST_ASSERT_EQUAL_UINT8(0, w.failures);
  TEST_ASSERT_TRUE(w.attempting);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_connect_posts_connecting_then_connected);
  RUN_TEST(test_drop_after_connected_retries_at_once);
  RUN_TEST(test_three_failures_report_failed_and_keep_retrying);
  RUN_TEST(test_wrong_password_fails_at_once_and_backs_off);
  RUN_TEST(test_scan_while_connecting_aborts_attempt_then_resumes);
  RUN_TEST(test_scan_pending_gives_up_waiting_after_three_seconds);
  RUN_TEST(test_scan_when_idle_or_connected_starts_at_once);
  RUN_TEST(test_attempt_timeout_counts_as_failure);
  RUN_TEST(test_disconnect_request_reports_off_and_stops);
  RUN_TEST(test_new_network_while_connected_disconnects_first);
  return UNITY_END();
}
