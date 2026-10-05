/* firmware/tests/test_display.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Asks, cards, images, posts and battery sense (core/src/display.c,
 * spec §4.5-§4.7, §5.4). */
#include <stdlib.h>
#include <string.h>
#include "fake_hal.h"
#include "gadget_ui.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) { fake_reset(); }
void tearDown(void) { core_deinit(); }

static const char *PERMISSION =
    "{\"op\":\"ask\",\"id\":\"a_1\",\"kind\":\"permission\",\"title\":\"Run a shell command?\",\"body\":\"ls -la\","
    "\"options\":[{\"id\":\"allow\",\"label\":\"Allow\",\"style\":\"allow\"},"
    "{\"id\":\"deny\",\"label\":\"Deny\",\"style\":\"deny\"}]}";

static void tap_at(gadget_rect_t r) {
  int16_t x = (int16_t)(r.x + r.w / 2), y = (int16_t)(r.y + r.h / 2);
  fake_input(GADGET_IN_TOUCH_DOWN, x, y);
  fake_input(GADGET_IN_TOUCH_UP, x, y);
  fake_run(10);
}

static void test_permission_ask_on_a_touch_board(void) {
  fake_ready("amoled-175c");
  fake_ws_in(PERMISSION);
  fake_run(10);
  const ui_model_t *m = core_ui_model();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_ASK, m->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_NONE, m->maus);
  TEST_ASSERT_EQUAL_STRING("Run a shell command?", m->ask.title);
  TEST_ASSERT_EQUAL_STRING("ls -la", m->ask.body);
  TEST_ASSERT_FALSE(m->ask.question);
  TEST_ASSERT_EQUAL_UINT8(2, m->ask.n_options);
  TEST_ASSERT_EQUAL_INT(UI_STYLE_ALLOW, m->ask.options[0].style);
  TEST_ASSERT_EQUAL_INT(UI_STYLE_DENY, m->ask.options[1].style);
  TEST_ASSERT_TRUE(m->ask.answerable);
  TEST_ASSERT_EQUAL_INT8(-1, m->ask.chosen);
  gadget_rect_t want[UI_ASK_OPTIONS_MAX];
  ui_layout_ask(gadget_board_by_id("amoled-175c"), 2, want);
  TEST_ASSERT_EQUAL_MEMORY(&want[1], &m->ask.options[1].rect, sizeof want[1]);
  tap_at(m->ask.options[1].rect); /* within the first 0.6 s: ignored */
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("answer"));
  fake_run(600);
  tap_at(m->ask.options[1].rect);
  cJSON *ans = fake_ws_last("answer");
  TEST_ASSERT_EQUAL_STRING("a_1", cJSON_GetObjectItem(ans, "id")->valuestring);
  TEST_ASSERT_EQUAL_STRING("deny", cJSON_GetObjectItem(ans, "option")->valuestring);
  cJSON_Delete(ans);
  TEST_ASSERT_EQUAL_INT8(1, core_ui_model()->ask.chosen);
  tap_at(m->ask.options[0].rect); /* answers at most once */
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("answer"));
  TEST_ASSERT_FALSE(fake_mic_running()); /* touches on the ask never record */
  fake_ws_in("{\"op\":\"ask.close\",\"id\":\"a_1\",\"reason\":\"answered\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_buttons_answer_on_a_button_board(void) {
  fake_ready("lcd-154");
  fake_ws_in(PERMISSION);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT16(0, core_ui_model()->ask.options[0].rect.w); /* no rectangles without touch */
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("answer"));
  TEST_ASSERT_FALSE(fake_mic_running());
  fake_run(600);
  fake_input(GADGET_IN_CANCEL_DOWN, 0, 0);
  fake_input(GADGET_IN_CANCEL_UP, 0, 0);
  cJSON *ans = fake_ws_last("answer");
  TEST_ASSERT_EQUAL_STRING("deny", cJSON_GetObjectItem(ans, "option")->valuestring);
  cJSON_Delete(ans);
  fake_ws_in("{\"op\":\"ask.close\",\"id\":\"a_1\",\"reason\":\"answered\"}");
  fake_ws_in("{\"op\":\"ask\",\"id\":\"a_2\",\"kind\":\"question\",\"title\":\"Which?\",\"body\":\"\","
             "\"options\":[{\"id\":\"x\",\"label\":\"Only one\"}]}");
  fake_run(700);
  fake_input(GADGET_IN_CANCEL_DOWN, 0, 0); /* a one-option ask maps only TALK */
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("answer"));
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  ans = fake_ws_last("answer");
  TEST_ASSERT_EQUAL_STRING("x", cJSON_GetObjectItem(ans, "option")->valuestring);
  cJSON_Delete(ans);
}

/* Listening outranks Ask (contract §2.7): an ask that arrives while TALK or a
 * touch is held stays hidden, and the press keeps its meaning until it ends. */
static void ask_arrives_while_recording(const char *board, gadget_input_type_t down) {
  fake_ready(board);
  fake_input(down, 100, 100);
  fake_mic_frames(25, 3000);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("voice.begin"));
  fake_ws_in(PERMISSION);
  fake_mic_frames(50, 3000);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_LISTENING, core_ui_model()->screen);
}

static void test_an_ask_never_takes_a_held_talk_release(void) {
  ask_arrives_while_recording("lcd-154", GADGET_IN_TALK_DOWN);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("voice.end"));
  TEST_ASSERT_FALSE(fake_mic_running());
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("answer"));
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_ASK, core_ui_model()->screen); /* now the person sees it */
}

static void test_cancel_while_recording_drops_it_and_never_answers(void) {
  ask_arrives_while_recording("lcd-154", GADGET_IN_TALK_DOWN);
  fake_input(GADGET_IN_CANCEL_DOWN, 0, 0);
  fake_input(GADGET_IN_CANCEL_UP, 0, 0);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("voice.drop"));
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("answer"));
  TEST_ASSERT_FALSE(fake_mic_running());
  fake_input(GADGET_IN_TALK_UP, 0, 0); /* TALK let go after the cancel: no turn, no answer */
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("voice.end"));
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("answer"));
}

static void test_an_ask_never_takes_a_held_touch_release(void) {
  ask_arrives_while_recording("amoled-175c", GADGET_IN_TOUCH_DOWN);
  fake_input(GADGET_IN_TOUCH_UP, 100, 100);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("voice.end"));
  TEST_ASSERT_FALSE(fake_mic_running());
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("answer"));
}

static void test_a_touch_lifted_before_300_ms_never_records_under_an_ask(void) {
  fake_ready("amoled-175c");
  fake_input(GADGET_IN_TOUCH_DOWN, 100, 100);
  fake_run(100);
  fake_ws_in(PERMISSION);
  fake_input(GADGET_IN_TOUCH_UP, 100, 100);
  fake_mic_frames(50, 3000);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("voice.begin"));
  TEST_ASSERT_FALSE(fake_mic_running());
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("answer"));
}

/* Contract §2.9: the touch-up that answers is the end of a touch that began
 * on the ask. A press that ends while the finger stays down (a swipe down,
 * the 60 s limit) leaves the rest of that touch to nobody: lifting it over an
 * ask that arrived meanwhile never answers. A fresh tap still does. */
static void test_a_touch_that_began_before_an_ask_never_answers_it(void) {
  gadget_rect_t r[UI_ASK_OPTIONS_MAX];
  ui_layout_ask(gadget_board_by_id("amoled-175c"), 2, r);
  int16_t ax = (int16_t)(r[0].x + r[0].w / 2), ay = (int16_t)(r[0].y + r[0].h / 2); /* Allow */
  /* a swipe down that began 50 ms before the ask, held past its 0.6 s lock */
  fake_ready("amoled-175c");
  fake_input(GADGET_IN_TOUCH_DOWN, ax, 100);
  fake_run(50);
  fake_ws_in(PERMISSION);
  fake_input(GADGET_IN_TOUCH_MOVE, ax, ay); /* a swipe down: it ends the press */
  fake_run(700);
  fake_input(GADGET_IN_TOUCH_UP, ax, ay);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_ASK, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("answer"));
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("voice.begin"));
  fake_run(700);
  tap_at(r[0]); /* a fresh tap answers */
  cJSON *ans = fake_ws_last("answer");
  TEST_ASSERT_NOT_NULL(ans);
  TEST_ASSERT_EQUAL_STRING("allow", cJSON_GetObjectItem(ans, "option")->valuestring);
  cJSON_Delete(ans);
  /* a hold past the 60 s limit, with the host's pings keeping the session alive */
  fake_reset();
  fake_ready("amoled-175c");
  fake_input(GADGET_IN_TOUCH_DOWN, ax, ay);
  for (int s = 0; s < 61; s++) {
    fake_mic_frames(50, 3000);
    fake_ws_ping_in();
  }
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("voice.end")); /* the limit ended the recording */
  TEST_ASSERT_FALSE(fake_mic_running());
  fake_ws_in(PERMISSION);
  fake_run(700);
  fake_input(GADGET_IN_TOUCH_UP, ax, ay);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_ASK, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("answer"));
  fake_run(700);
  tap_at(r[0]);
  ans = fake_ws_last("answer");
  TEST_ASSERT_NOT_NULL(ans);
  TEST_ASSERT_EQUAL_STRING("allow", cJSON_GetObjectItem(ans, "option")->valuestring);
  cJSON_Delete(ans);
}

/* Spec §5.4: presses in the first 0.6 s after an ask appears are ignored. An
 * ask that waited behind Listening appears when the recording ends. */
static void test_an_ask_shown_after_listening_ignores_presses_for_0_6_s(void) {
  ask_arrives_while_recording("lcd-154", GADGET_IN_TALK_DOWN);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  fake_run(100);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_ASK, core_ui_model()->screen);
  fake_input(GADGET_IN_TALK_DOWN, 0, 0); /* 0.1 s after it appeared: ignored */
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("answer"));
  TEST_ASSERT_FALSE(fake_mic_running());
  fake_run(500);
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  cJSON *ans = fake_ws_last("answer");
  TEST_ASSERT_NOT_NULL(ans);
  TEST_ASSERT_EQUAL_STRING("allow", cJSON_GetObjectItem(ans, "option")->valuestring);
  cJSON_Delete(ans);
}

static void test_unanswerable_asks(void) {
  fake_ready("lcd-154");
  fake_ws_in("{\"op\":\"ask\",\"id\":\"a_3\",\"kind\":\"question\",\"title\":\"Pick\",\"body\":\"\",\"options\":["
             "{\"id\":\"1\",\"label\":\"One\"},{\"id\":\"2\",\"label\":\"Two\"},{\"id\":\"3\",\"label\":\"Three\"}]}");
  fake_run(700);
  TEST_ASSERT_FALSE(core_ui_model()->ask.answerable); /* more than two options on buttons */
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_mic_frames(20, 3000);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_LISTENING, core_ui_model()->screen); /* TALK talks instead */
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("answer"));
  fake_reset();
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"ask\",\"id\":\"a_4\",\"kind\":\"question\",\"title\":\"Long form\",\"body\":\"\",\"options\":[]}");
  fake_run(10);
  TEST_ASSERT_FALSE(core_ui_model()->ask.answerable);
  TEST_ASSERT_TRUE(core_ui_model()->ask.question);
  fake_ws_in("{\"op\":\"ask.close\",\"id\":\"a_4\",\"reason\":\"withdrawn\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_asks_queue_one_at_a_time(void) {
  fake_ready("amoled-175c");
  fake_ws_in(PERMISSION);
  fake_ws_in(PERMISSION); /* sent again: not a second entry */
  fake_ws_in("{\"op\":\"ask\",\"id\":\"a_2\",\"kind\":\"permission\",\"title\":\"Second\",\"body\":\"\",\"options\":["
             "{\"id\":\"allow\",\"label\":\"Allow\",\"style\":\"allow\"},{\"id\":\"deny\",\"label\":\"Deny\",\"style\":\"deny\"}]}");
  fake_run(1000);
  TEST_ASSERT_EQUAL_STRING("a_1", core_ui_model()->ask.id);
  TEST_ASSERT_EQUAL_UINT8(1, core_ui_model()->ask.queued);
  fake_ws_in("{\"op\":\"ask.close\",\"id\":\"a_1\",\"reason\":\"answered\"}");
  TEST_ASSERT_EQUAL_STRING("a_2", core_ui_model()->ask.id);
  TEST_ASSERT_EQUAL_UINT64(fake_now() + 600, core_ui_model()->ask.locked_until_ms); /* a fresh lock */
  fake_ws_drop(1006);
  fake_run(10);
  TEST_ASSERT_EQUAL_UINT8(0, core_ui_model()->ask.n_options); /* the host sends open asks again */
  fake_run(2000);
  fake_handshake();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_ask_expiry(void) {
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"ask\",\"id\":\"a_5\",\"kind\":\"permission\",\"title\":\"Quick\",\"body\":\"\",\"options\":["
             "{\"id\":\"allow\",\"label\":\"Allow\"},{\"id\":\"deny\",\"label\":\"Deny\"}],\"expires_s\":5}");
  fake_run(4900);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_ASK, core_ui_model()->screen);
  fake_run(200);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_cards_show_expire_and_dismiss(void) {
  fake_ready("lcd-154");
  fake_ws_in("{\"op\":\"card\",\"id\":\"c1\",\"title\":\"Build passed\",\"body\":\"main is green\",\"ttl_s\":3}");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_CARD, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_STRING("Build passed", core_ui_model()->card.title);
  fake_run(3000);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
  fake_ws_in("{\"op\":\"card\",\"id\":\"c2\",\"title\":\"Sticky\",\"body\":\"\",\"ttl_s\":0}");
  for (int i = 0; i < 6; i++) {
    fake_run(10000);
    fake_ws_ping_in(); /* the host's ping keeps the session alive */
  }
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_CARD, core_ui_model()->screen); /* ttl 0: until dismissed */
  fake_ws_ping_in();
  fake_input(GADGET_IN_CANCEL_DOWN, 0, 0);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
  fake_ws_in("{\"op\":\"card\",\"id\":\"c3\",\"title\":\"x\",\"body\":\"\"}");
  fake_ws_in("{\"op\":\"card.close\",\"id\":\"c3\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void send_image(uint8_t stream, uint16_t w, uint16_t h, size_t rows_bytes) {
  char begin[160];
  snprintf(begin, sizeof begin, "{\"op\":\"image.begin\",\"id\":\"i1\",\"stream\":%u,\"w\":%u,\"h\":%u,\"ttl_s\":0}",
           (unsigned)stream, (unsigned)w, (unsigned)h);
  fake_ws_in(begin);
  static uint8_t frame[2 + 8190];
  size_t sent = 0;
  while (sent < rows_bytes) {
    size_t n = rows_bytes - sent > 8190 ? 8190 : rows_bytes - sent;
    frame[0] = 0x03;
    frame[1] = stream;
    for (size_t i = 0; i < n; i++) frame[2 + i] = (uint8_t)((sent + i) & 0xff);
    fake_ws_bin_in(frame, 2 + n);
    sent += n;
  }
  char end[64];
  snprintf(end, sizeof end, "{\"op\":\"image.end\",\"stream\":%u}", (unsigned)stream);
  fake_ws_in(end);
  fake_run(10);
}

static void test_images(void) {
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"card\",\"id\":\"c1\",\"title\":\"Under\",\"body\":\"\"}");
  send_image(5, 300, 200, 300u * 200u * 2u);
  const ui_model_t *m = core_ui_model();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IMAGE, m->screen); /* an image shows over a card */
  TEST_ASSERT_EQUAL_UINT16(300, m->image.w);
  TEST_ASSERT_NOT_NULL(m->image.pixels);
  const uint8_t *bytes = (const uint8_t *)(const void *)m->image.pixels;
  TEST_ASSERT_EQUAL_HEX8(0x00, bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x07, bytes[8199]); /* (8190 + 9) & 0xff: rows continue across frames */
  uint32_t rev = m->image.pixels_rev;
  fake_ws_in("{\"op\":\"card.close\",\"id\":\"i1\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_CARD, core_ui_model()->screen);
  TEST_ASSERT_TRUE(core_ui_model()->image.pixels_rev != rev);
  TEST_ASSERT_NULL(core_ui_model()->image.pixels);
  send_image(6, 301, 200, 301u * 200u * 2u); /* larger than caps.image: ignored */
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_CARD, core_ui_model()->screen);
  send_image(7, 100, 100, 1000); /* incomplete: dropped */
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_CARD, core_ui_model()->screen);
}

/* Ports send raw touch events (contract §2.5). A swipe down worked out from
 * them dismisses the image, then the card, on a board with no CANCEL button. */
static void swipe_down_by_touch(void) {
  fake_input(GADGET_IN_TOUCH_DOWN, 233, 100);
  fake_run(100);
  fake_input(GADGET_IN_TOUCH_MOVE, 233, 210); /* 110 px down within 300 ms */
  fake_input(GADGET_IN_TOUCH_UP, 233, 250);
  fake_run(10);
}

static void test_a_touch_swipe_dismisses_the_image_then_the_card(void) {
  fake_ready("amoled-175");
  fake_ws_in("{\"op\":\"card\",\"id\":\"c1\",\"title\":\"Under\",\"body\":\"\"}");
  send_image(5, 100, 100, 100u * 100u * 2u);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IMAGE, core_ui_model()->screen);
  swipe_down_by_touch();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_CARD, core_ui_model()->screen);
  swipe_down_by_touch();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("voice.begin"));
}

static void test_posts_toast_and_chime(void) {
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"post\",\"id\":\"p1\",\"bot\":{\"id\":\"b_jev\",\"name\":\"Jev\"},\"kind\":\"routine\","
             "\"text\":\"Morning brief is ready\",\"speak\":false}");
  fake_run(10);
  const ui_model_t *m = core_ui_model();
  TEST_ASSERT_TRUE(m->toast.visible);
  TEST_ASSERT_EQUAL_INT(UI_POST_ROUTINE, m->toast.kind);
  TEST_ASSERT_EQUAL_STRING("Jev", m->toast.bot_name);
  TEST_ASSERT_EQUAL_STRING("Morning brief is ready", m->toast.text);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, m->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_NOTIFYING, m->maus);
  fake_run(400);
  TEST_ASSERT_EQUAL_size_t(4800, fake_spk_accepted()); /* 300 ms at 16 kHz */
  TEST_ASSERT_TRUE(fake_spk_peak() > 5000 && fake_spk_peak() <= 6000);
  fake_run(7700);
  TEST_ASSERT_FALSE(core_ui_model()->toast.visible);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_IDLE, core_ui_model()->maus);
  /* a tap hides the toast */
  fake_ws_in("{\"op\":\"post\",\"id\":\"p2\",\"bot\":{\"id\":\"b_jev\",\"name\":\"Jev\"},\"kind\":\"message\","
             "\"text\":\"Done\",\"speak\":false}");
  fake_input(GADGET_IN_TOUCH_DOWN, 233, 233);
  fake_input(GADGET_IN_TOUCH_UP, 233, 233);
  fake_run(10);
  TEST_ASSERT_FALSE(core_ui_model()->toast.visible);
}

static void test_spoken_post_chimes_then_speaks(void) {
  fake_ready("amoled-175c");
  int stops = fake_spk_stops();
  fake_ws_in("{\"op\":\"post\",\"id\":\"p1\",\"bot\":{\"id\":\"b_jev\",\"name\":\"Jev\"},\"kind\":\"message\","
             "\"text\":\"Your build passed\",\"speak\":true}");
  fake_ws_in("{\"op\":\"speak.begin\",\"stream\":2,\"rate\":16000}"); /* no turn: the post's speech */
  static uint8_t frame[2 + 1280];
  frame[0] = 0x02;
  frame[1] = 2;
  for (int i = 0; i < 640; i++) frame[2 + 2 * i] = (uint8_t)(i % 2 ? 0x40 : 0xc0);
  for (int i = 0; i < 10; i++) fake_ws_bin_in(frame, sizeof frame); /* 10 x 40 ms */
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":2}");
  fake_run(1500);
  TEST_ASSERT_TRUE(fake_spk_accepted() >= 4800 + 6400); /* the whole chime, then the speech */
  TEST_ASSERT_EQUAL_INT(stops, fake_spk_stops());        /* the chime was never cut */
  const ui_model_t *m = core_ui_model();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, m->screen);
  TEST_ASSERT_TRUE(m->toast.visible);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_NOTIFYING, m->maus);
}

static void test_no_chime_over_speech(void) {
  fake_ready("amoled-175c");
  fake_console_in("say \"hi\"");
  cJSON *say = fake_ws_last("say");
  char json[160];
  snprintf(json, sizeof json, "{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}",
           cJSON_GetObjectItem(say, "turn")->valuestring);
  cJSON_Delete(say);
  fake_ws_in(json);
  static uint8_t frame[2 + 1280];
  frame[0] = 0x02;
  frame[1] = 1;
  for (int i = 0; i < 10; i++) {
    fake_ws_bin_in(frame, sizeof frame);
    fake_run(40);
  }
  fake_ws_in("{\"op\":\"post\",\"id\":\"p1\",\"bot\":{\"id\":\"b\",\"name\":\"Jev\"},\"kind\":\"message\",\"text\":\"x\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_SPEAKING, core_ui_model()->screen); /* the speech keeps playing */
  TEST_ASSERT_EQUAL_INT16(0, fake_spk_peak());                     /* and no chime was mixed in */
}

/* Never play into a live mic: neither in the 300 ms before a held press
 * records (the mic already runs and pre-buffers) nor while it records. */
static void spoken_post_while_talk_is_held(int mic_frames_before) {
  fake_ready("lcd-154");
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_mic_frames(mic_frames_before, 3000);
  fake_ws_in("{\"op\":\"post\",\"id\":\"p1\",\"bot\":{\"id\":\"b_jev\",\"name\":\"Jev\"},\"kind\":\"message\","
             "\"text\":\"Your build passed\",\"speak\":true}");
  fake_ws_in("{\"op\":\"speak.begin\",\"stream\":2,\"rate\":16000}"); /* no turn: the post's speech */
  static uint8_t frame[2 + 1280];
  frame[0] = 0x02;
  frame[1] = 2;
  for (int i = 0; i < 640; i++) frame[2 + 2 * i] = (uint8_t)(i % 2 ? 0x40 : 0xc0);
  for (int i = 0; i < 25; i++) { /* 25 x 40 ms */
    fake_ws_bin_in(frame, sizeof frame);
    fake_mic_frames(2, 3000);
  }
  TEST_ASSERT_EQUAL_size_t(0, fake_spk_accepted());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_LISTENING, core_ui_model()->screen);
  TEST_ASSERT_TRUE(core_ui_model()->toast.visible); /* the post still shows */
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("voice.end"));
}

static void test_no_playback_into_a_live_mic(void) {
  spoken_post_while_talk_is_held(5);  /* 100 ms: held, not recording yet */
  fake_reset();
  spoken_post_while_talk_is_held(25); /* 500 ms: recording */
}

static void test_battery_and_sense(void) {
  fake_battery_set(true, 82, false);
  fake_ready("amoled-175c");
  fake_run(1000);
  TEST_ASSERT_TRUE(core_ui_model()->battery.present);
  TEST_ASSERT_EQUAL_UINT8(82, core_ui_model()->battery.pct);
  fake_battery_set(true, 81, false);
  for (int i = 0; i < 8; i++) {
    fake_run(1000);
    fake_ws_ping_in();
  }
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("sense")); /* at most one every 10 s */
  fake_run(2000);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("sense"));
  cJSON *s = fake_ws_last("sense");
  TEST_ASSERT_EQUAL_INT(81, cJSON_GetObjectItem(s, "battery_pct")->valueint);
  TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(s, "charging")));
  cJSON_Delete(s);
  fake_run(10000);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("sense")); /* nothing changed */
  fake_battery_set(true, 81, true);
  fake_ws_ping_in();
  fake_run(1100);
  TEST_ASSERT_EQUAL_size_t(2, fake_ws_count("sense"));
}

static void test_boards_without_a_battery_never_sense(void) {
  fake_ready("devkit");
  fake_run(30000);
  TEST_ASSERT_FALSE(core_ui_model()->battery.present);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("sense"));
}

int main(void) {
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_permission_ask_on_a_touch_board);
  RUN_TEST(test_buttons_answer_on_a_button_board);
  RUN_TEST(test_an_ask_never_takes_a_held_talk_release);
  RUN_TEST(test_cancel_while_recording_drops_it_and_never_answers);
  RUN_TEST(test_an_ask_never_takes_a_held_touch_release);
  RUN_TEST(test_a_touch_lifted_before_300_ms_never_records_under_an_ask);
  RUN_TEST(test_a_touch_that_began_before_an_ask_never_answers_it);
  RUN_TEST(test_an_ask_shown_after_listening_ignores_presses_for_0_6_s);
  RUN_TEST(test_unanswerable_asks);
  RUN_TEST(test_asks_queue_one_at_a_time);
  RUN_TEST(test_ask_expiry);
  RUN_TEST(test_cards_show_expire_and_dismiss);
  RUN_TEST(test_images);
  RUN_TEST(test_a_touch_swipe_dismisses_the_image_then_the_card);
  RUN_TEST(test_posts_toast_and_chime);
  RUN_TEST(test_spoken_post_chimes_then_speaks);
  RUN_TEST(test_no_chime_over_speech);
  RUN_TEST(test_no_playback_into_a_live_mic);
  RUN_TEST(test_battery_and_sense);
  RUN_TEST(test_boards_without_a_battery_never_sense);
  return UNITY_END();
}
