/* firmware/tests/test_console.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Console commands and @omb lines (core/src/console_cmd.c, spec §5.6). */
#include <string.h>
#include "fake_hal.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) { fake_reset(); }
void tearDown(void) { core_deinit(); }

static const char *last_line(void) { return fake_console_line(fake_console_count() - 1); }

static void test_status_of_a_fresh_gadget(void) {
  fake_battery_set(true, 82, false);
  fake_boot("amoled-175c");
  fake_console_in("status");
  char want[400];
  snprintf(want, sizeof want,
           "@omb {\"op\":\"status\",\"wifi\":\"connected\",\"id\":\"%s\",\"pair\":\"unpaired\",\"fw\":\"1.0.0\","
           "\"battery\":{\"pct\":82,\"charging\":false},\"board\":\"amoled-175c\",\"name\":\"Maus %.4s\"}",
           core_device_id(), core_device_id() + 4);
  TEST_ASSERT_EQUAL_STRING(want, last_line());
}

static void test_status_of_a_paired_gadget(void) {
  fake_storage_put(GADGET_KEY_WIFI_SSID, "Home");
  fake_ready("devkit");
  fake_console_in("status");
  TEST_ASSERT_EQUAL_STRING(
      "@omb {\"op\":\"status\",\"wifi\":\"connected\",\"ssid\":\"Home\",\"host\":\"127.0.0.1:8810\",\"id\":\"" FAKE_RFC_ID
      "\",\"pair\":\"paired\",\"fw\":\"1.0.0\",\"board\":\"devkit\",\"name\":\"Maus b18b\",\"host_name\":\"Mac\"}",
      last_line());
}

static void test_status_after_bad_code_then_a_new_pair(void) {
  fake_storage_put(GADGET_KEY_HOST_ADDR, "192.168.1.20:8810");
  fake_boot("lcd-154");
  fake_console_in("pair 111111");
  fake_run(10);
  fake_ws_accept();
  fake_ws_in("{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"" FAKE_HOST_ID "\",\"host_name\":\"Mac\"}");
  fake_ws_in("{\"op\":\"error\",\"code\":\"bad_code\"}");
  fake_run(10);
  fake_console_in("status");
  cJSON *st = fake_omb("status");
  TEST_ASSERT_EQUAL_STRING("error", cJSON_GetObjectItem(st, "pair")->valuestring);
  TEST_ASSERT_EQUAL_STRING("bad_code", cJSON_GetObjectItem(st, "error")->valuestring);
  TEST_ASSERT_EQUAL_STRING("Mac", cJSON_GetObjectItem(st, "host_name")->valuestring); /* from the challenge */
  cJSON_Delete(st);
  int opens = fake_ws_opens();
  fake_console_in("pair 222222");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(opens + 1, fake_ws_opens()); /* reconnects at once */
  fake_console_in("status");
  st = fake_omb("status");
  TEST_ASSERT_EQUAL_STRING("connecting", cJSON_GetObjectItem(st, "pair")->valuestring);
  TEST_ASSERT_NULL(cJSON_GetObjectItem(st, "error"));
  cJSON_Delete(st);
  fake_ws_accept();
  fake_ws_in("{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"" FAKE_HOST_ID "\",\"host_name\":\"Mac\"}");
  cJSON *prove = fake_ws_last("prove");
  TEST_ASSERT_EQUAL_STRING("222222", cJSON_GetObjectItem(prove, "enroll")->valuestring);
  cJSON_Delete(prove);
}

static void test_code_stored_before_a_host_is_known(void) {
  fake_wifi_set(GADGET_WIFI_OFF);
  fake_boot("lcd-154");
  fake_console_in("pair 123456");
  TEST_ASSERT_EQUAL_STRING("123456", fake_storage_str(GADGET_KEY_PAIR_CODE));
  fake_console_in("status");
  cJSON *st = fake_omb("status");
  TEST_ASSERT_EQUAL_STRING("code_stored", cJSON_GetObjectItem(st, "pair")->valuestring);
  TEST_ASSERT_EQUAL_STRING("off", cJSON_GetObjectItem(st, "wifi")->valuestring);
  cJSON_Delete(st);
}

static void test_wifi_host_and_name_are_stored(void) {
  fake_boot("lcd-154");
  fake_console_in("wifi \"My Home\" \"pass word\"");
  TEST_ASSERT_EQUAL_STRING("My Home", fake_storage_str(GADGET_KEY_WIFI_SSID));
  TEST_ASSERT_EQUAL_STRING("pass word", fake_storage_str(GADGET_KEY_WIFI_PASS));
  TEST_ASSERT_EQUAL_STRING("My Home", fake_wifi_ssid());
  fake_console_in("host omkars-mac.local:9000");
  TEST_ASSERT_EQUAL_STRING("omkars-mac.local:9000", fake_storage_str(GADGET_KEY_HOST_ADDR));
  fake_console_in("name \"Desk Maus\"");
  TEST_ASSERT_EQUAL_STRING("Desk Maus", fake_storage_str(GADGET_KEY_NAME));
  TEST_ASSERT_EQUAL_STRING("Desk Maus", core_ui_model()->device_name);
  fake_console_in("pair 123456");
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING("omkars-mac.local", fake_ws_host());
  TEST_ASSERT_EQUAL_UINT16(9000, fake_ws_port());
  fake_ws_accept();
  cJSON *hello = fake_ws_last("hello");
  TEST_ASSERT_EQUAL_STRING("Desk Maus", cJSON_GetObjectItem(hello, "name")->valuestring);
  cJSON_Delete(hello);
  fake_console_in("host auto");
  fake_run(10);
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_HOST_ADDR));
  TEST_ASSERT_EQUAL_INT(1, fake_mdns_browses()); /* the open session was dropped and mDNS asked */
}

static void test_host_and_host_auto_drop_the_challenge_host_name(void) {
  static const char *CHALLENGE_MAC =
      "{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"" FAKE_HOST_ID "\",\"host_name\":\"Mac\"}";
  fake_storage_put(GADGET_KEY_HOST_ADDR, "192.168.1.20:8810");
  fake_storage_put(GADGET_KEY_PAIR_CODE, "111111");
  fake_boot("lcd-154");
  fake_ws_accept();
  fake_ws_in(CHALLENGE_MAC);
  fake_ws_in("{\"op\":\"error\",\"code\":\"bad_code\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING("Mac", core_ui_model()->host_name);
  fake_console_in("pair 222222");
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING("Mac", core_ui_model()->host_name); /* a new code keeps it: same host */
  fake_console_in("host 192.168.1.30");
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING("", core_ui_model()->host_name); /* another host: its name is not known yet */
  TEST_ASSERT_EQUAL_STRING("192.168.1.30", fake_ws_host());
  fake_ws_accept();
  fake_ws_in(CHALLENGE_MAC);
  TEST_ASSERT_EQUAL_STRING("Mac", core_ui_model()->host_name);
  fake_console_in("host auto");
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING("", core_ui_model()->host_name);
}

static void test_say_sends_a_typed_turn(void) {
  fake_boot("lcd-154");
  fake_console_in("say \"hello\"");
  TEST_ASSERT_EQUAL_STRING("@omb {\"op\":\"error\",\"cmd\":\"say\",\"message\":\"not connected to MausBot\"}",
                           last_line());
  fake_reset();
  fake_ready("lcd-154");
  fake_console_in("say \"What's on today?\"");
  cJSON *say = fake_ws_last("say");
  TEST_ASSERT_NOT_NULL(say);
  TEST_ASSERT_EQUAL_STRING("What's on today?", cJSON_GetObjectItem(say, "text")->valuestring);
  cJSON *line = fake_omb("say");
  TEST_ASSERT_EQUAL_STRING(cJSON_GetObjectItem(say, "turn")->valuestring, cJSON_GetObjectItem(line, "turn")->valuestring);
  cJSON_Delete(say);
  cJSON_Delete(line);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_THINKING, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_STRING("What's on today?", core_ui_model()->thinking.heard);
}

static void test_scan_prints_one_line(void) {
  fake_boot("lcd-154");
  fake_console_in("scan");
  TEST_ASSERT_EQUAL_INT(1, fake_wifi_scans());
  gadget_wifi_ap_t aps[2] = {{"Home", -52, GADGET_AUTH_WPA2}, {"Cafe", -80, GADGET_AUTH_OPEN}};
  fake_wifi_scan_result(aps, 2, true);
  TEST_ASSERT_EQUAL_STRING("@omb {\"op\":\"scan\",\"networks\":[{\"ssid\":\"Home\",\"rssi\":-52,\"auth\":\"wpa2\"},"
                           "{\"ssid\":\"Cafe\",\"rssi\":-80,\"auth\":\"open\"}]}",
                           last_line());
  fake_wifi_scan_result(NULL, 0, false);
  TEST_ASSERT_EQUAL_STRING("@omb {\"op\":\"scan\",\"networks\":[]}", last_line());
}

/* Contract §2.11: `scan` always ends in one `@omb scan` line, also when
 * hal_wifi_scan() refuses to start, the same line a failed scan event gives. */
static void test_a_scan_that_cannot_start_prints_no_networks(void) {
  fake_boot("lcd-154");
  fake_wifi_start_fails(true);
  size_t n = fake_console_count();
  fake_console_in("scan");
  TEST_ASSERT_EQUAL_INT(1, fake_wifi_scans());
  TEST_ASSERT_EQUAL_size_t(n + 1, fake_console_count());
  TEST_ASSERT_EQUAL_STRING("@omb {\"op\":\"scan\",\"networks\":[]}", last_line());
}

/* Contract deviations, item 5: Wi-Fi that cannot start prints an error line. */
static void test_wifi_that_cannot_start_prints_an_error(void) {
  fake_boot("lcd-154");
  fake_wifi_start_fails(true);
  fake_console_in("wifi \"My Home\" \"pass word\"");
  TEST_ASSERT_EQUAL_STRING("@omb {\"op\":\"error\",\"cmd\":\"wifi\",\"message\":\"could not start Wi-Fi\"}", last_line());
  TEST_ASSERT_EQUAL_STRING("My Home", fake_storage_str(GADGET_KEY_WIFI_SSID)); /* kept for the next boot */
}

static void test_errors_are_reported(void) {
  fake_boot("lcd-154");
  fake_console_in("pair 12");
  TEST_ASSERT_EQUAL_STRING("@omb {\"op\":\"error\",\"cmd\":\"pair\",\"message\":\"pair needs a six-digit code\"}",
                           last_line());
  fake_console_in("dance");
  TEST_ASSERT_EQUAL_STRING("@omb {\"op\":\"error\",\"cmd\":\"dance\",\"message\":\"unknown command; try status\"}",
                           last_line());
  size_t n = fake_console_count();
  fake_console_in("");
  TEST_ASSERT_EQUAL_size_t(n, fake_console_count()); /* an empty line prints nothing */
  core_event(&(gadget_event_t){.type = GADGET_EV_CONSOLE_LINE, .u.console.line = NULL});
  TEST_ASSERT_EQUAL_STRING("@omb {\"op\":\"error\",\"cmd\":\"\",\"message\":\"line too long\"}", last_line());
}

static void test_log_off_and_on(void) {
  fake_boot("lcd-154");
  fake_console_in("log off");
  TEST_ASSERT_FALSE(fake_log_enabled());
  fake_console_in("status"); /* @omb lines still print */
  TEST_ASSERT_NOT_NULL(fake_omb("status"));
  fake_console_in("log on");
  TEST_ASSERT_TRUE(fake_log_enabled());
}

static void test_forget_erases_everything_and_restarts(void) {
  fake_storage_put(GADGET_KEY_WIFI_SSID, "Home");
  fake_ready("amoled-175c");
  FAKE_EXPECT_RESTART(fake_console_in("forget"));
  TEST_ASSERT_EQUAL_INT(1, fake_restarts());
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_DEV_KEY));
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_HOST_ID));
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_WIFI_SSID));
  fake_boot("amoled-175c"); /* a new identity */
  TEST_ASSERT_TRUE(strcmp(FAKE_RFC_ID, core_device_id()) != 0);
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_UNPAIRED, core_pair_state());
}

static void test_reboot_keeps_storage(void) {
  fake_ready("amoled-175c");
  FAKE_EXPECT_RESTART(fake_console_in("reboot"));
  TEST_ASSERT_TRUE(fake_storage_has(GADGET_KEY_HOST_ID));
}

int main(void) {
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_status_of_a_fresh_gadget);
  RUN_TEST(test_status_of_a_paired_gadget);
  RUN_TEST(test_status_after_bad_code_then_a_new_pair);
  RUN_TEST(test_code_stored_before_a_host_is_known);
  RUN_TEST(test_wifi_host_and_name_are_stored);
  RUN_TEST(test_host_and_host_auto_drop_the_challenge_host_name);
  RUN_TEST(test_say_sends_a_typed_turn);
  RUN_TEST(test_scan_prints_one_line);
  RUN_TEST(test_a_scan_that_cannot_start_prints_no_networks);
  RUN_TEST(test_wifi_that_cannot_start_prints_an_error);
  RUN_TEST(test_errors_are_reported);
  RUN_TEST(test_log_off_and_on);
  RUN_TEST(test_forget_erases_everything_and_restarts);
  RUN_TEST(test_reboot_keeps_storage);
  return UNITY_END();
}
