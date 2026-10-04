/* firmware/tests/test_turns.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Voice and typed turns (core/src/interaction.c, spec §4.4, §5.4). */
#include <stdlib.h>
#include <string.h>
#include "fake_hal.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) { fake_reset(); }
void tearDown(void) { core_deinit(); }

static char *str_of(cJSON *j, const char *key) {
  static char buf[256];
  snprintf(buf, sizeof buf, "%s", cJSON_GetObjectItem(j, key)->valuestring);
  cJSON_Delete(j);
  return buf;
}

static const char *last_turn(const char *op) { return str_of(fake_ws_last(op), "turn"); }

static void host(const char *fmt, const char *turn) {
  char json[512];
  snprintf(json, sizeof json, fmt, turn);
  fake_ws_in(json);
}

/* Hold TALK for frames x 20 ms with a tone, then release. Returns the turn. */
static const char *talk(int frames) {
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_mic_frames(frames, 3000);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  fake_run(10);
  return last_turn("voice.begin");
}

static void test_a_held_talk_records_and_sends(void) {
  fake_ready("lcd-154");
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  TEST_ASSERT_TRUE(fake_mic_running());
  fake_mic_frames(14, 3000); /* 280 ms: not a press yet */
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("voice.begin"));
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
  fake_mic_frames(36, 3000); /* 1 s in all */
  cJSON *vb = fake_ws_last("voice.begin");
  TEST_ASSERT_NOT_NULL(vb);
  TEST_ASSERT_EQUAL_INT(1, cJSON_GetObjectItem(vb, "stream")->valueint);
  TEST_ASSERT_EQUAL_INT(16000, cJSON_GetObjectItem(vb, "rate")->valueint);
  const char *turn = cJSON_GetObjectItem(vb, "turn")->valuestring;
  TEST_ASSERT_EQUAL_size_t(11, strlen(turn)); /* "t" + 8 hex + "-1" */
  TEST_ASSERT_EQUAL_CHAR('t', turn[0]);
  TEST_ASSERT_EQUAL_STRING("-1", turn + 9);
  cJSON_Delete(vb);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_LISTENING, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_LISTENING, core_ui_model()->maus);
  TEST_ASSERT_TRUE(core_ui_model()->mic_level > 0);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  fake_run(10);
  TEST_ASSERT_FALSE(fake_mic_running());
  TEST_ASSERT_EQUAL_size_t(50, fake_ws_bin_sent()); /* the 15 buffered frames are sent too */
  size_t len = 0;
  const uint8_t *f = fake_ws_bin(0, &len);
  TEST_ASSERT_EQUAL_size_t(642, len);
  TEST_ASSERT_EQUAL_HEX8(0x01, f[0]);
  TEST_ASSERT_EQUAL_HEX8(1, f[1]);
  /* -3000 little-endian: 0x48 0xF4 */
  TEST_ASSERT_EQUAL_HEX8(0x48, f[2]);
  TEST_ASSERT_EQUAL_HEX8(0xF4, f[3]);
  cJSON *ve = fake_ws_last("voice.end");
  TEST_ASSERT_EQUAL_INT(1000, cJSON_GetObjectItem(ve, "ms")->valueint);
  cJSON_Delete(ve);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_THINKING, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_THINKING, core_ui_model()->maus);
}

static void test_a_short_press_is_ignored(void) {
  fake_ready("amoled-175c");
  fake_input(GADGET_IN_TOUCH_DOWN, 200, 200);
  fake_mic_frames(10, 3000);
  fake_input(GADGET_IN_TOUCH_UP, 200, 200);
  fake_run(500);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_sent());
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_bin_sent());
  TEST_ASSERT_FALSE(fake_mic_running());
}

static void test_turn_messages_drive_the_screens(void) {
  fake_ready("amoled-175c");
  const char *turn = talk(30);
  char t[33];
  strcpy(t, turn);
  host("{\"op\":\"heard\",\"turn\":\"%s\",\"text\":\"What's on today?\"}", t);
  TEST_ASSERT_EQUAL_STRING("What's on today?", core_ui_model()->thinking.heard);
  host("{\"op\":\"working\",\"turn\":\"%s\",\"text\":\"checking your calendar\"}", t);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_WORKING, core_ui_model()->maus);
  host("{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"You have\"}", t);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, core_ui_model()->screen);
  TEST_ASSERT_FALSE(core_ui_model()->reply.final);
  host("{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"You have two meetings.\",\"final\":true}", t);
  host("{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"ok\"}", t);
  fake_run(10);
  const ui_model_t *m = core_ui_model();
  TEST_ASSERT_EQUAL_STRING("You have two meetings.", m->reply.text);
  TEST_ASSERT_TRUE(m->reply.final);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, m->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_IDLE, m->maus);
  fake_run(19900);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, core_ui_model()->screen);
  fake_run(200);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen); /* 20 s after done */
}

static void test_frames_for_other_turns_are_ignored(void) {
  fake_ready("amoled-175c");
  talk(30);
  fake_ws_in("{\"op\":\"reply\",\"turn\":\"t00000000-9\",\"text\":\"not ours\"}");
  fake_ws_in("{\"op\":\"done\",\"turn\":\"t00000000-9\",\"outcome\":\"ok\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_THINKING, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_STRING("", core_ui_model()->reply.text);
}

static void test_failed_and_stopped_turns(void) {
  fake_ready("amoled-175c");
  char t[33];
  strcpy(t, talk(30));
  host("{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"failed\",\"reason\":\"Didn't catch that\"}", t);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_ALERTING, core_ui_model()->maus);
  TEST_ASSERT_TRUE(core_ui_model()->reply.failed);
  TEST_ASSERT_EQUAL_STRING("Didn't catch that", core_ui_model()->reply.reason);
  strcpy(t, talk(30));
  host("{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"stopped\"}", t);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen); /* nothing to show */
}

static void test_a_long_reply_keeps_its_tail(void) {
  fake_ready("amoled-175c");
  char t[33];
  strcpy(t, talk(30));
  static char json[12000];
  int n = snprintf(json, sizeof json, "{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"", t);
  for (int i = 0; i < 9990; i++) json[n++] = (char)('a' + i % 26);
  strcpy(json + n, "END\"}");
  fake_ws_in(json);
  const char *text = core_ui_model()->reply.text;
  TEST_ASSERT_EQUAL_size_t(UI_REPLY_MAX - 1, strlen(text));
  TEST_ASSERT_EQUAL_MEMORY("\xe2\x80\xa6", text, 3); /* cut from the start with "…" */
  TEST_ASSERT_EQUAL_STRING("END", text + strlen(text) - 3);
}

static void test_text_outside_the_charset_is_folded(void) {
  fake_ready("amoled-175c");
  char t[33];
  strcpy(t, talk(30));
  host("{\"op\":\"heard\",\"turn\":\"%s\",\"text\":\"caf\xc3\xa9 \xe2\x98\x95 \xe2\x86\x92 ok\xe2\x80\xa6\"}", t);
  TEST_ASSERT_EQUAL_STRING("caf\xc3\xa9 ? \xe2\x86\x92 ok\xe2\x80\xa6", core_ui_model()->thinking.heard);
}

static void test_tap_on_a_reply_goes_back_to_idle(void) {
  fake_ready("amoled-175c");
  char t[33];
  strcpy(t, talk(30));
  host("{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"Hi\",\"final\":true}", t);
  host("{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"ok\"}", t);
  fake_run(10);
  fake_input(GADGET_IN_TOUCH_DOWN, 100, 100);
  fake_run(100);
  fake_input(GADGET_IN_TOUCH_UP, 100, 100);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_cancel_while_recording_drops_the_utterance(void) {
  fake_ready("lcd-154");
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_mic_frames(30, 3000);
  fake_input(GADGET_IN_CANCEL_DOWN, 0, 0);
  fake_input(GADGET_IN_CANCEL_UP, 0, 0);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  fake_run(10);
  cJSON *vb = fake_ws_last("voice.begin");
  cJSON *vd = fake_ws_last("voice.drop");
  TEST_ASSERT_NOT_NULL(vd);
  TEST_ASSERT_EQUAL_STRING(cJSON_GetObjectItem(vb, "turn")->valuestring, cJSON_GetObjectItem(vd, "turn")->valuestring);
  cJSON_Delete(vb);
  cJSON_Delete(vd);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("voice.end"));
  TEST_ASSERT_FALSE(fake_mic_running());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_swipe_down_while_recording_drops_it(void) {
  fake_ready("amoled-175c");
  fake_input(GADGET_IN_TOUCH_DOWN, 233, 150);
  fake_mic_frames(30, 3000);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("voice.begin"));
  fake_input(GADGET_IN_TOUCH_MOVE, 236, 260); /* 110 px down, more than 466 / 8 */
  fake_input(GADGET_IN_TOUCH_UP, 236, 300);
  fake_run(10);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("voice.drop"));
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("voice.end"));
}

static void test_cancel_while_thinking_sends_stop_once(void) {
  fake_ready("lcd-154");
  char t[33];
  strcpy(t, talk(30));
  fake_input(GADGET_IN_CANCEL_DOWN, 0, 0);
  fake_swipe(GADGET_SWIPE_DOWN);
  fake_run(10);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("stop"));
  TEST_ASSERT_EQUAL_STRING(t, last_turn("stop"));
}

static void test_recording_stops_at_60_s_with_a_countdown(void) {
  fake_ready("lcd-154");
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  for (int i = 0; i < 5; i++) {
    fake_mic_frames(500, 1000); /* 10 s, with the host's ping that keeps the session alive */
    fake_ws_ping_in();
  }
  fake_mic_frames(249, 1000); /* 54.98 s */
  fake_ws_ping_in();
  TEST_ASSERT_EQUAL_UINT8(0, core_ui_model()->listening.countdown_s);
  fake_mic_frames(2, 1000);    /* 55.02 s */
  TEST_ASSERT_EQUAL_UINT8(5, core_ui_model()->listening.countdown_s);
  fake_mic_frames(200, 1000);  /* 59.02 s */
  TEST_ASSERT_EQUAL_UINT8(1, core_ui_model()->listening.countdown_s);
  fake_mic_frames(100, 1000);  /* past 60 s */
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("voice.end"));
  cJSON *ve = fake_ws_last("voice.end");
  int ms = cJSON_GetObjectItem(ve, "ms")->valueint;
  cJSON_Delete(ve);
  TEST_ASSERT_TRUE(ms >= 59960 && ms <= 60000);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_THINKING, core_ui_model()->screen);
  fake_input(GADGET_IN_TALK_UP, 0, 0); /* the late release does nothing */
  fake_run(10);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("voice.end"));
}

static void test_a_new_turn_stops_the_old_one_first(void) {
  fake_ready("lcd-154");
  char old[33];
  strcpy(old, talk(30));
  fake_ws_clear();
  talk(30);
  TEST_ASSERT_EQUAL_STRING(old, str_of(cJSON_Parse(fake_ws_text(0)), "turn")); /* stop for the old turn ... */
  cJSON *first = cJSON_Parse(fake_ws_text(0));
  TEST_ASSERT_EQUAL_STRING("stop", cJSON_GetObjectItem(first, "op")->valuestring);
  cJSON_Delete(first);
  cJSON *second = cJSON_Parse(fake_ws_text(1));
  TEST_ASSERT_EQUAL_STRING("voice.begin", cJSON_GetObjectItem(second, "op")->valuestring); /* ... then the new turn */
  cJSON_Delete(second);
  host("{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"late\"}", old);
  TEST_ASSERT_EQUAL_STRING("", core_ui_model()->reply.text);
}

static void test_a_dropped_session_ends_the_turn_locally(void) {
  fake_ready("amoled-175c");
  talk(30);
  fake_ws_drop(1006);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_OFFLINE, core_ui_model()->screen);
  TEST_ASSERT_TRUE(core_ui_model()->reply.failed);
  TEST_ASSERT_EQUAL_STRING("Connection lost", core_ui_model()->reply.reason);
  /* recording when the drop happens: mic stops, nothing resumes */
  fake_run(2000);
  fake_handshake();
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_mic_frames(30, 3000);
  fake_ws_drop(1006);
  fake_run(10);
  TEST_ASSERT_FALSE(fake_mic_running());
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  fake_run(10);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("voice.end"));
}

static void test_talk_reconnects_after_replaced(void) {
  fake_ready("lcd-154");
  fake_ws_in("{\"op\":\"error\",\"code\":\"replaced\"}");
  fake_run(5000);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(2, fake_ws_opens());
  TEST_ASSERT_FALSE(fake_mic_running()); /* that press only woke the gadget */
  /* on a touch board, touching the screen is the TALK press */
  fake_reset();
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"error\",\"code\":\"replaced\"}");
  fake_run(5000);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
  fake_input(GADGET_IN_TOUCH_DOWN, 200, 200);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(2, fake_ws_opens());
  TEST_ASSERT_FALSE(fake_mic_running());
  fake_input(GADGET_IN_TOUCH_UP, 200, 200);
}

static void test_mic_level_follows_the_signal(void) {
  fake_ready("lcd-154");
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_mic_frames(1, 328); /* -40 dBFS */
  TEST_ASSERT_EQUAL_UINT8(85, core_ui_model()->mic_level);
  fake_mic_frames(1, 32767);
  TEST_ASSERT_EQUAL_UINT8(255, core_ui_model()->mic_level);
  fake_mic_frames(1, 0);
  TEST_ASSERT_EQUAL_UINT8(0, core_ui_model()->mic_level);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
}

static void test_nothing_records_without_a_session(void) {
  fake_boot("lcd-154"); /* unpaired */
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_mic_frames(30, 3000);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  TEST_ASSERT_FALSE(fake_mic_running());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_SETUP, core_ui_model()->screen);
}

int main(void) {
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_a_held_talk_records_and_sends);
  RUN_TEST(test_a_short_press_is_ignored);
  RUN_TEST(test_turn_messages_drive_the_screens);
  RUN_TEST(test_frames_for_other_turns_are_ignored);
  RUN_TEST(test_failed_and_stopped_turns);
  RUN_TEST(test_a_long_reply_keeps_its_tail);
  RUN_TEST(test_text_outside_the_charset_is_folded);
  RUN_TEST(test_tap_on_a_reply_goes_back_to_idle);
  RUN_TEST(test_cancel_while_recording_drops_the_utterance);
  RUN_TEST(test_swipe_down_while_recording_drops_it);
  RUN_TEST(test_cancel_while_thinking_sends_stop_once);
  RUN_TEST(test_recording_stops_at_60_s_with_a_countdown);
  RUN_TEST(test_a_new_turn_stops_the_old_one_first);
  RUN_TEST(test_a_dropped_session_ends_the_turn_locally);
  RUN_TEST(test_talk_reconnects_after_replaced);
  RUN_TEST(test_mic_level_follows_the_signal);
  RUN_TEST(test_nothing_records_without_a_session);
  return UNITY_END();
}
