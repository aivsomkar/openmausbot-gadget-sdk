/* SPDX-License-Identifier: Apache-2.0 */
/* Exact screen copy (contract 2.15), without LVGL. */
#include <string.h>

#include "ui_copy.h"
#include "unity.h"

static ui_model_t m;
static ui_copy_t c;
static char line[UI_COPY_MAX];

void setUp(void) {
  memset(&m, 0, sizeof m);
  memset(&c, 0, sizeof c);
  line[0] = '\0';
}
void tearDown(void) {}

static void test_idle_greets_the_bot_or_just_hi(void) {
  ui_copy_idle(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("Hi", line);
  strcpy(m.bot_name, "Jev");
  ui_copy_idle(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("Hi, I'm Jev", line);
}

static void test_setup_copy_for_every_step(void) {
  strcpy(m.device_id, "gad_b18b86ce1389e46d");
  m.setup.step = UI_SETUP_NEED_WIFI;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Connect me to Wi-Fi with the installer.", c.caption);
  TEST_ASSERT_EQUAL_STRING("", c.host); /* no challenge yet */
  TEST_ASSERT_EQUAL_STRING("gad_b18b86ce1389e46d", c.status);

  m.setup.step = UI_SETUP_NEED_CODE;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Pair me: MausBot \xE2\x86\x92 Settings \xE2\x86\x92 Remote access \xE2\x86\x92 Pair a gadget\n"
                           "Enter the code in the installer.\n"
                           "Remote access must be on.", c.caption);

  m.setup.step = UI_SETUP_PAIRING;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Pairing\xE2\x80\xA6", c.caption);
  strcpy(m.host_name, "Omkar's computer");
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Pairing with Omkar's computer\xE2\x80\xA6", c.caption);
  TEST_ASSERT_EQUAL_STRING("", c.host); /* already in the caption */

  m.setup.step = UI_SETUP_HOST_NOT_FOUND;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Can't find MausBot. Enter the address shown under Pair a gadget in the installer.", c.caption);

  m.setup.step = UI_SETUP_BAD_CODE;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_STRING("That code didn't work. Get a new one from Pair a gadget.", c.caption);
  TEST_ASSERT_EQUAL_STRING("Omkar's computer", c.host); /* spec 5.5: once a challenge has arrived */
  TEST_ASSERT_EQUAL_STRING("gad_b18b86ce1389e46d", c.status);

  m.setup.step = UI_SETUP_DEVICE_LIMIT;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_STRING("MausBot has too many devices. Remove one in Remote access. Retrying\xE2\x80\xA6", c.caption);
  TEST_ASSERT_EQUAL_STRING("Omkar's computer", c.host);
}

static void test_offline_copy_for_every_reason(void) {
  m.now_ms = 10000;
  m.offline.reason = UI_OFFLINE_WIFI_CONNECTING;
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Connecting to Wi-Fi\xE2\x80\xA6", c.caption);
  TEST_ASSERT_EQUAL_STRING("", c.host); /* nothing stored yet */
  TEST_ASSERT_EQUAL_STRING("", c.status);

  m.offline.reason = UI_OFFLINE_WIFI_FAILED;
  strcpy(m.offline.ssid, "Home 5G");
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Can't join Home 5G.", c.caption);

  m.offline.reason = UI_OFFLINE_HOST_LOOKUP;
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Looking for MausBot\xE2\x80\xA6", c.caption);

  m.offline.reason = UI_OFFLINE_HOST_UNREACHABLE;
  strcpy(m.host_name, "Omkar's computer");
  m.offline.retry_at_ms = 16000;
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Can't reach Omkar's computer.\nIs Remote access on in MausBot?\n"
                           "On Windows, set this network to Private.", c.caption);
  TEST_ASSERT_EQUAL_STRING("", c.host); /* already in the caption */
  TEST_ASSERT_EQUAL_STRING("Retrying in 6 s", c.status);

  m.offline.reason = UI_OFFLINE_IN_USE_ELSEWHERE;
  m.offline.retry_at_ms = 0;
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("In use elsewhere\nPress TALK to use it here.", c.caption);
  TEST_ASSERT_EQUAL_STRING("Omkar's computer", c.host); /* spec 5.5: the stored host_name */
  TEST_ASSERT_EQUAL_STRING("", c.status);

  m.offline.reason = UI_OFFLINE_PROTOCOL;
  m.offline.retry_at_ms = 12500;
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("MausBot didn't accept me.", c.caption);
  TEST_ASSERT_EQUAL_STRING("Omkar's computer", c.host);
  TEST_ASSERT_EQUAL_STRING("Retrying in 3 s", c.status);
  m.offline.reason = UI_OFFLINE_HOST_LOOKUP;
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Omkar's computer", c.host);
}

static void test_offline_without_names_falls_back(void) {
  m.offline.reason = UI_OFFLINE_HOST_UNREACHABLE;
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Can't reach MausBot.\nIs Remote access on in MausBot?\n"
                           "On Windows, set this network to Private.", c.caption);
  TEST_ASSERT_EQUAL_STRING("", c.status); /* retry_at_ms 0: no automatic retry */
  m.offline.reason = UI_OFFLINE_WIFI_FAILED;
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Can't join Wi-Fi.", c.caption);
}

static void test_retry_seconds_round_up_and_never_show_zero(void) {
  TEST_ASSERT_EQUAL_UINT32(6, ui_retry_seconds(10000, 16000));
  TEST_ASSERT_EQUAL_UINT32(6, ui_retry_seconds(10001, 16000));
  TEST_ASSERT_EQUAL_UINT32(1, ui_retry_seconds(15999, 16000));
  TEST_ASSERT_EQUAL_UINT32(1, ui_retry_seconds(16000, 16000));
  TEST_ASSERT_EQUAL_UINT32(1, ui_retry_seconds(20000, 16000));
  TEST_ASSERT_EQUAL_UINT32(60, ui_retry_seconds(0, 60000));
}

static void test_update_copy_for_every_phase(void) {
  m.update.phase = UI_UPDATE_RECEIVING;
  m.update.pct = 42;
  ui_copy_update(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("Updating\xE2\x80\xA6 42%", line);
  m.update.pct = 250; /* never more than 100 % */
  ui_copy_update(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("Updating\xE2\x80\xA6 100%", line);
  m.update.phase = UI_UPDATE_VERIFYING;
  ui_copy_update(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("Checking update\xE2\x80\xA6", line);
  m.update.phase = UI_UPDATE_RESTARTING;
  ui_copy_update(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("Restarting\xE2\x80\xA6", line);
}

static void test_countdown_shows_5_to_1_only(void) {
  m.listening.countdown_s = 0;
  ui_copy_countdown(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("", line);
  m.listening.countdown_s = 5;
  ui_copy_countdown(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("5", line);
  m.listening.countdown_s = 1;
  ui_copy_countdown(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("1", line);
  m.listening.countdown_s = 9;
  ui_copy_countdown(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("", line);
}

static void test_long_names_never_overflow(void) {
  memset(m.bot_name, 'x', sizeof m.bot_name - 1);
  m.bot_name[sizeof m.bot_name - 1] = '\0';
  ui_copy_idle(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_size_t(strlen("Hi, I'm ") + sizeof m.bot_name - 1, strlen(line));
  memset(m.host_name, 'y', sizeof m.host_name - 1);
  m.host_name[sizeof m.host_name - 1] = '\0';
  m.setup.step = UI_SETUP_PAIRING;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_TRUE(strlen(c.caption) < sizeof c.caption);
  m.setup.step = UI_SETUP_BAD_CODE;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_size_t(sizeof m.host_name - 1, strlen(c.host));
}

static void test_out_of_range_enums_give_defined_text(void) {
  memset(&c, 'x', sizeof c);
  m.offline.reason = (ui_offline_reason_t)42;
  strcpy(m.host_name, "Omkar's computer");
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("", c.caption);
  TEST_ASSERT_EQUAL_STRING("", c.host);
  TEST_ASSERT_EQUAL_STRING("", c.status);
  memset(&c, 'x', sizeof c);
  strcpy(m.device_id, "gad_b18b86ce1389e46d");
  m.setup.step = (ui_setup_step_t)42;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_STRING("", c.caption);
  TEST_ASSERT_EQUAL_STRING("", c.host);
  TEST_ASSERT_EQUAL_STRING("gad_b18b86ce1389e46d", c.status);
  memset(line, 'x', sizeof line);
  m.update.phase = (ui_update_phase_t)42;
  ui_copy_update(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("", line);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_greets_the_bot_or_just_hi);
  RUN_TEST(test_setup_copy_for_every_step);
  RUN_TEST(test_offline_copy_for_every_reason);
  RUN_TEST(test_offline_without_names_falls_back);
  RUN_TEST(test_retry_seconds_round_up_and_never_show_zero);
  RUN_TEST(test_update_copy_for_every_phase);
  RUN_TEST(test_countdown_shows_5_to_1_only);
  RUN_TEST(test_long_names_never_overflow);
  RUN_TEST(test_out_of_range_enums_give_defined_text);
  return UNITY_END();
}
