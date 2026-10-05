/* firmware/tests/test_audio.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Speech playback, the jitter buffer, barge-in, tap-to-stop and the mouth
 * level (core/src/audio.c, spec §4.4, §5.4, contract §2.7). */
#include <string.h>
#include "fake_hal.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) { fake_reset(); }
void tearDown(void) { core_deinit(); }

static char g_turn[33];

static void host(const char *fmt, const char *turn) {
  char json[512];
  snprintf(json, sizeof json, fmt, turn);
  fake_ws_in(json);
}

/* A typed turn (shorter to set up than a recording). */
static void start_turn(void) {
  fake_console_in("say \"hi\"");
  cJSON *say = fake_ws_last("say");
  snprintf(g_turn, sizeof g_turn, "%s", cJSON_GetObjectItem(say, "turn")->valuestring);
  cJSON_Delete(say);
}

/* One 40 ms speaker frame of a square wave (640 samples at 16 kHz). */
static void speech_frame(uint8_t stream, int16_t amp, uint32_t rate) {
  static uint8_t frame[2 + 960 * 2];
  size_t samples = rate / 25u;
  frame[0] = 0x02;
  frame[1] = stream;
  for (size_t i = 0; i < samples; i++) {
    int16_t v = (i / 10) % 2 ? amp : (int16_t)-amp;
    frame[2 + 2 * i] = (uint8_t)((uint16_t)v & 0xff);
    frame[3 + 2 * i] = (uint8_t)((uint16_t)v >> 8);
  }
  fake_ws_bin_in(frame, 2 + samples * 2);
}

/* frames x 40 ms of speech, paced in real time like the host. */
static void speak(uint8_t stream, int frames, int16_t amp) {
  for (int i = 0; i < frames; i++) {
    speech_frame(stream, amp, 16000);
    fake_run(40);
  }
}

static void test_speech_plays_and_the_screen_follows(void) {
  fake_ready("amoled-175c");
  start_turn();
  host("{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"You have two meetings.\",\"final\":true}", g_turn);
  host("{\"op\":\"speak.begin\",\"stream\":4,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  TEST_ASSERT_EQUAL_UINT32(16000, fake_spk_rate());
  speak(4, 3, 8000); /* 120 ms: still pre-buffering */
  TEST_ASSERT_EQUAL_size_t(0, fake_spk_accepted());
  speak(4, 22, 8000); /* 1 s in all */
  TEST_ASSERT_TRUE(fake_spk_accepted() > 0);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_SPEAKING, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_SPEAKING, core_ui_model()->maus);
  TEST_ASSERT_TRUE(core_ui_model()->reply.speak_elapsed_ms > 0);
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":4}");
  TEST_ASSERT_EQUAL_UINT32(1000, core_ui_model()->reply.speak_total_ms);
  host("{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"ok\"}", g_turn);
  fake_run(600);
  TEST_ASSERT_EQUAL_size_t(16000, fake_spk_accepted()); /* every sample reached the speaker */
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_UINT8(0, core_ui_model()->speak_level);
  fake_run(19000);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, core_ui_model()->screen); /* 20 s from the end of speech */
  fake_run(1500);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

/* Contract §2.8: speak_elapsed_ms and speak_total_ms belong to the reply's
 * speech stream. A post's chime and speech belong to no turn: they leave
 * both alone and do not keep the reply up. */
static void test_a_post_leaves_the_reply_speech_alone(void) {
  fake_ready("amoled-175c");
  start_turn();
  host("{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"You have two meetings.\",\"final\":true}", g_turn);
  host("{\"op\":\"speak.begin\",\"stream\":4,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(4, 25, 8000); /* 1 s */
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":4}");
  host("{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"ok\"}", g_turn);
  for (int i = 0; i < 200 && core_ui_model()->screen == UI_SCREEN_SPEAKING; i++) fake_run(10);
  uint64_t speech_ended = fake_now();
  const ui_model_t *m = core_ui_model();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, m->screen);
  TEST_ASSERT_EQUAL_size_t(16000, fake_spk_accepted());
  TEST_ASSERT_EQUAL_UINT32(1000, m->reply.speak_total_ms);
  TEST_ASSERT_EQUAL_UINT32(1000, m->reply.speak_elapsed_ms);
  fake_run(5000);
  fake_ws_in("{\"op\":\"post\",\"id\":\"p1\",\"bot\":{\"id\":\"b_jev\",\"name\":\"Jev\"},\"kind\":\"message\","
             "\"text\":\"Your build passed\",\"speak\":true}");
  fake_ws_in("{\"op\":\"speak.begin\",\"stream\":5,\"rate\":16000}"); /* no turn: the post's speech */
  speak(5, 10, 8000);
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":5}");
  fake_run(1000);
  TEST_ASSERT_EQUAL_size_t(16000 + 4800 + 6400, fake_spk_accepted()); /* the chime and the post's speech played */
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, m->screen);
  TEST_ASSERT_EQUAL_UINT32(1000, m->reply.speak_total_ms);
  TEST_ASSERT_EQUAL_UINT32(1000, m->reply.speak_elapsed_ms);
  fake_run((uint32_t)(speech_ended + 19900 - fake_now()));
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, core_ui_model()->screen);
  fake_run(200);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen); /* 20 s after the reply's speech, not the post's */
}

static void test_speak_level_rises_at_once_and_falls_a_step_per_60_ms(void) {
  fake_ready("amoled-175c");
  start_turn();
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(1, 15, 16000); /* about -6 dBFS: level 3 */
  TEST_ASSERT_EQUAL_UINT8(3, core_ui_model()->speak_level);
  /* silence follows; sample the level every 10 ms while it plays out */
  uint8_t prev = 3;
  uint64_t last_drop = 0;
  int drops = 0;
  for (int i = 0; i < 25; i++) {
    speech_frame(1, 0, 16000);
    for (int k = 0; k < 4; k++) {
      fake_run(10);
      uint8_t lv = core_ui_model()->speak_level;
      TEST_ASSERT_TRUE_MESSAGE(lv <= prev, "the level rose during silence");
      if (lv < prev) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(prev - 1, lv, "the level fell more than one step");
        if (drops > 0) TEST_ASSERT_TRUE_MESSAGE(fake_now() - last_drop >= 60, "steps closer than 60 ms");
        last_drop = fake_now();
        drops++;
      }
      prev = lv;
    }
  }
  TEST_ASSERT_EQUAL_INT(3, drops);
  TEST_ASSERT_EQUAL_UINT8(0, core_ui_model()->speak_level);
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":1}");
  fake_run(1000);
  /* quiet speech maps to level 1 */
  start_turn();
  host("{\"op\":\"speak.begin\",\"stream\":2,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(2, 15, 400); /* about -38 dBFS */
  TEST_ASSERT_EQUAL_UINT8(1, core_ui_model()->speak_level);
}

static void test_tap_stops_speech_and_the_turn(void) {
  fake_ready("amoled-175c");
  start_turn();
  host("{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"Long answer\"}", g_turn);
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(1, 10, 8000);
  int stops = fake_spk_stops();
  fake_input(GADGET_IN_TOUCH_DOWN, 233, 233);
  fake_run(50);
  TEST_ASSERT_EQUAL_UINT8(0, core_ui_model()->speak_level); /* the mouth closes with the speech */
  fake_input(GADGET_IN_TOUCH_UP, 233, 233);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(stops + 1, fake_spk_stops());
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("stop")); /* done has not arrived: stop the turn too */
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, core_ui_model()->screen); /* the reply stays for a second tap */
  host("{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"stopped\"}", g_turn);
  fake_input(GADGET_IN_TOUCH_DOWN, 233, 233);
  fake_input(GADGET_IN_TOUCH_UP, 233, 233);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_tap_after_done_only_stops_playback(void) {
  fake_ready("amoled-175c");
  start_turn();
  host("{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"Done talking\",\"final\":true}", g_turn);
  host("{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"ok\"}", g_turn);
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(1, 10, 8000);
  fake_input(GADGET_IN_TOUCH_DOWN, 233, 233);
  fake_input(GADGET_IN_TOUCH_UP, 233, 233);
  fake_run(10);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("stop"));
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, core_ui_model()->screen);
}

static void test_barge_in_stops_the_old_turn_before_the_new_one(void) {
  fake_ready("lcd-154");
  start_turn();
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(1, 10, 8000);
  fake_ws_clear();
  int stops = fake_spk_stops();
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  TEST_ASSERT_EQUAL_INT(stops + 1, fake_spk_stops()); /* playback stops at once */
  fake_mic_frames(30, 3000);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  fake_run(10);
  cJSON *first = cJSON_Parse(fake_ws_text(0));
  TEST_ASSERT_EQUAL_STRING("stop", cJSON_GetObjectItem(first, "op")->valuestring);
  TEST_ASSERT_EQUAL_STRING(g_turn, cJSON_GetObjectItem(first, "turn")->valuestring);
  cJSON_Delete(first);
  cJSON *second = cJSON_Parse(fake_ws_text(1));
  TEST_ASSERT_EQUAL_STRING("voice.begin", cJSON_GetObjectItem(second, "op")->valuestring);
  cJSON_Delete(second);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("stop"));
}

static void test_cancel_stops_speech(void) {
  fake_ready("lcd-154");
  start_turn();
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(1, 10, 8000);
  fake_input(GADGET_IN_CANCEL_DOWN, 0, 0);
  fake_run(10);
  TEST_ASSERT_NOT_EQUAL(UI_SCREEN_SPEAKING, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("stop"));
}

static void test_speak_stop_and_replacement_streams(void) {
  fake_ready("amoled-175c");
  start_turn();
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(1, 10, 8000);
  TEST_ASSERT_EQUAL_UINT8(3, core_ui_model()->speak_level);
  fake_ws_in("{\"op\":\"speak.stop\",\"stream\":1}");
  fake_run(10);
  TEST_ASSERT_NOT_EQUAL(UI_SCREEN_SPEAKING, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_UINT8(0, core_ui_model()->speak_level); /* at once, not a step per 60 ms */
  /* a new speak.begin replaces the playing stream; frames for the old one are dropped */
  host("{\"op\":\"speak.begin\",\"stream\":2,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(2, 10, 8000);
  host("{\"op\":\"speak.begin\",\"stream\":3,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  size_t before = fake_spk_accepted();
  speak(2, 10, 8000);
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":3}");
  fake_run(1000);
  TEST_ASSERT_EQUAL_size_t(before, fake_spk_accepted());
}

/* A press shorter than 300 ms is a tap, not a recording: speech that begins
 * while it is held waits (never into the live mic) and plays once it ends. */
static void test_speech_that_begins_during_a_tap_still_plays(void) {
  fake_ready("amoled-175c");
  start_turn();
  fake_input(GADGET_IN_TOUCH_DOWN, 233, 233);
  fake_run(50);
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  for (int i = 0; i < 6; i++) speech_frame(1, 8000, 16000); /* 240 ms at once: past the pre-buffer */
  fake_run(50);
  TEST_ASSERT_EQUAL_size_t(0, fake_spk_accepted()); /* the mic is live */
  TEST_ASSERT_NOT_EQUAL(UI_SCREEN_SPEAKING, core_ui_model()->screen);
  fake_input(GADGET_IN_TOUCH_UP, 233, 233); /* 100 ms: a tap */
  speak(1, 9, 8000);
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":1}");
  fake_run(1000);
  TEST_ASSERT_EQUAL_size_t(9600, fake_spk_accepted()); /* all 15 frames */
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("voice.begin"));
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("stop"));
}

static void test_speech_for_another_turn_is_ignored(void) {
  fake_ready("amoled-175c");
  start_turn();
  fake_ws_in("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"t00000000-1\"}");
  speak(1, 10, 8000);
  TEST_ASSERT_EQUAL_size_t(0, fake_spk_accepted());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_THINKING, core_ui_model()->screen);
}

static void test_devkit_plays_24_khz(void) {
  fake_ready("devkit");
  start_turn();
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":24000,\"turn\":\"%s\"}", g_turn);
  TEST_ASSERT_EQUAL_UINT32(24000, fake_spk_rate());
  for (int i = 0; i < 10; i++) {
    speech_frame(1, 8000, 24000);
    fake_run(40);
  }
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":1}");
  fake_run(1000);
  TEST_ASSERT_EQUAL_size_t(9600, fake_spk_accepted());
}

static void test_a_flood_beyond_the_buffer_is_dropped(void) {
  fake_ready("amoled-175c");
  start_turn();
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  for (int i = 0; i < 100; i++) speech_frame(1, 8000, 16000); /* 4 s at once, no pacing */
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":1}");
  fake_run(3000);
  TEST_ASSERT_TRUE(fake_spk_accepted() <= 16000u + 3200u); /* the 1 s buffer plus what the speaker took */
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_PAIRED, core_pair_state());
}

static void test_a_frame_of_exactly_8_kib_plays(void) {
  fake_ready("amoled-175c");
  static uint8_t frame[GADGET_BINARY_FRAME_MAX]; /* the protocol limit, header included */
  memset(frame, 0x11, sizeof frame);
  frame[0] = 0x02; /* speaker audio, stream 2 */
  frame[1] = 2;
  fake_ws_in("{\"op\":\"speak.begin\",\"stream\":2,\"rate\":16000}");
  fake_ws_bin_in(frame, sizeof frame);
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":2}");
  fake_run(500);
  TEST_ASSERT_EQUAL_size_t((GADGET_BINARY_FRAME_MAX - 2) / 2, fake_spk_accepted()); /* 4095 samples */
}

static void test_a_drop_stops_playback(void) {
  fake_ready("amoled-175c");
  start_turn();
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(1, 10, 8000);
  int stops = fake_spk_stops();
  TEST_ASSERT_EQUAL_UINT8(3, core_ui_model()->speak_level);
  fake_ws_drop(1006);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(stops + 1, fake_spk_stops());
  TEST_ASSERT_EQUAL_UINT8(0, core_ui_model()->speak_level); /* contract §2.7: 0 when no stream plays */
  fake_run(300);
  TEST_ASSERT_EQUAL_UINT8(0, core_ui_model()->speak_level); /* the mouth closes */
}

int main(void) {
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_speech_plays_and_the_screen_follows);
  RUN_TEST(test_a_post_leaves_the_reply_speech_alone);
  RUN_TEST(test_speak_level_rises_at_once_and_falls_a_step_per_60_ms);
  RUN_TEST(test_tap_stops_speech_and_the_turn);
  RUN_TEST(test_tap_after_done_only_stops_playback);
  RUN_TEST(test_barge_in_stops_the_old_turn_before_the_new_one);
  RUN_TEST(test_cancel_stops_speech);
  RUN_TEST(test_speak_stop_and_replacement_streams);
  RUN_TEST(test_speech_that_begins_during_a_tap_still_plays);
  RUN_TEST(test_speech_for_another_turn_is_ignored);
  RUN_TEST(test_devkit_plays_24_khz);
  RUN_TEST(test_a_flood_beyond_the_buffer_is_dropped);
  RUN_TEST(test_a_frame_of_exactly_8_kib_plays);
  RUN_TEST(test_a_drop_stops_playback);
  return UNITY_END();
}
