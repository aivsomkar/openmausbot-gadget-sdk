/* SPDX-License-Identifier: Apache-2.0 */
/* Golden PNGs of the screens a scripted host cannot reach without knowing
 * the gadget's random turn id (thinking, speaking, reply), plus every copy
 * variant the simulator scripts skip. Drawn by the real UI through the
 * simulator's headless display (round mask included) on the virtual clock
 * with seed 1. Run from the repository root: argv[1] = board id. */
#include <stdio.h>
#include <string.h>

#include "gadget_board.h"
#include "gadget_ui.h"
#include "lvgl.h"
#include "sim_display.h"
#include "sim_hal.h"
#include "unity.h"

void sim_post_event(const gadget_event_t *ev) { (void)ev; }

static const gadget_board_t *board;
static ui_model_t m;
static uint64_t now;
static char dir[96];

static const char *const k_reply =
  "You have two meetings today: design review at 10 and lunch with Sam at 1. "
  "The review moved to room Atlas, and Sam asked whether you can bring the printed slides. "
  "After that your afternoon is free until the 4 pm call with the Berlin team.";

void setUp(void) {
  sim_display_opts_t o = {true, 1.0f, dir};
  TEST_ASSERT_EQUAL_INT(0, sim_display_init(board, &o));
  TEST_ASSERT_EQUAL_INT(GADGET_OK, ui_init(board, 1));
  uint32_t rev = m.rev;
  memset(&m, 0, sizeof m);
  m.rev = rev;
  strcpy(m.device_id, "gad_b18b86ce1389e46d");
  strcpy(m.bot_name, "Jev");
  strcpy(m.host_name, "Omkar's computer");
  m.battery.present = board->has_battery;
  m.battery.pct = 82;
  now = 0;
}

void tearDown(void) {
  ui_deinit();
  sim_display_deinit();
}

/* Show the model from t = 0 to 3 s in 10 ms steps, then compare. */
static void shot(const char *name) {
  m.rev++;
  for (; now <= 3000; now += 10) {
    sim_display_advance(10);
    m.now_ms = now;
    ui_render(&m);
    ui_tick(now);
  }
  char msg[320];
  int r = sim_display_snapshot(name);
  snprintf(msg, sizeof msg, "%s/%s.png: %s", dir, name,
           r == 2 ? "missing (build with -DGADGET_SNAPSHOT_UPDATE=ON to create it)" : "differs (see the _err.png next to it)");
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, r, msg);
}

static void test_thinking(void) {
  m.screen = UI_SCREEN_THINKING;
  m.maus = UI_MAUS_THINKING;
  strcpy(m.thinking.heard, "What's on my calendar today?");
  shot("fx_thinking");
}

static void test_thinking_working(void) {
  m.screen = UI_SCREEN_THINKING;
  m.maus = UI_MAUS_WORKING;
  strcpy(m.thinking.heard, "What's on my calendar today?");
  strcpy(m.thinking.working, "checking your calendar");
  shot("fx_thinking_working");
}

static void speaking(uint8_t level, const char *name) {
  m.screen = UI_SCREEN_SPEAKING;
  m.maus = UI_MAUS_SPEAKING;
  m.speak_level = level;
  strcpy(m.reply.text, k_reply);
  shot(name);
}
static void test_speaking_1(void) { speaking(1, "fx_speaking_1"); }
static void test_speaking_2(void) { speaking(2, "fx_speaking_2"); }
static void test_speaking_3(void) { speaking(3, "fx_speaking_3"); }

static void test_speaking_second_page(void) {
  m.reply.speak_total_ms = 12000;
  m.reply.speak_elapsed_ms = 7000;
  speaking(2, "fx_speaking_page2");
}

static void test_reply(void) {
  m.screen = UI_SCREEN_REPLY;
  m.maus = UI_MAUS_IDLE;
  strcpy(m.reply.text, k_reply);
  m.reply.final = true;
  shot("fx_reply");
}

static void test_reply_failed(void) {
  m.screen = UI_SCREEN_REPLY;
  m.maus = UI_MAUS_ALERTING;
  m.reply.failed = true;
  strcpy(m.reply.reason, "Didn't catch that");
  shot("fx_reply_failed");
}

/* The simulator always has Wi-Fi (contract 2.5), so no script reaches this step. */
static void test_setup_need_wifi(void) {
  m.screen = UI_SCREEN_SETUP;
  m.maus = UI_MAUS_CURIOUS;
  m.setup.step = UI_SETUP_NEED_WIFI;
  m.host_name[0] = '\0'; /* no challenge yet */
  shot("fx_setup_need_wifi");
}

static void test_setup_host_not_found(void) {
  m.screen = UI_SCREEN_SETUP;
  m.maus = UI_MAUS_CURIOUS;
  m.setup.step = UI_SETUP_HOST_NOT_FOUND;
  m.host_name[0] = '\0'; /* `host auto` found nothing: no challenge yet */
  shot("fx_setup_host_not_found");
}

static void offline(ui_offline_reason_t reason, uint64_t retry_at, const char *name) {
  m.screen = UI_SCREEN_OFFLINE;
  m.maus = UI_MAUS_SLEEPING;
  m.offline.reason = reason;
  strcpy(m.offline.ssid, "Home");
  m.offline.retry_at_ms = retry_at;
  shot(name);
}
static void test_offline_wifi_connecting(void) { offline(UI_OFFLINE_WIFI_CONNECTING, 0, "fx_offline_wifi_connecting"); }
static void test_offline_wifi_failed(void) { offline(UI_OFFLINE_WIFI_FAILED, 0, "fx_offline_wifi_failed"); }
static void test_offline_looking(void) { offline(UI_OFFLINE_HOST_LOOKUP, 0, "fx_offline_looking"); }
static void test_offline_protocol(void) { offline(UI_OFFLINE_PROTOCOL, 9000, "fx_offline_protocol"); }

static void test_update_verifying(void) {
  m.screen = UI_SCREEN_UPDATE;
  m.update.phase = UI_UPDATE_VERIFYING;
  m.update.pct = 100;
  shot("fx_update_verifying");
}

static void test_update_restarting(void) {
  m.screen = UI_SCREEN_UPDATE;
  m.update.phase = UI_UPDATE_RESTARTING;
  m.update.pct = 100;
  shot("fx_update_restarting");
}

static void test_ask_answered_here(void) {
  m.screen = UI_SCREEN_ASK;
  strcpy(m.ask.title, "Run shell command?");
  strcpy(m.ask.body, "ls -la ~/Documents");
  m.ask.n_options = 2;
  gadget_rect_t r[UI_ASK_OPTIONS_MAX] = {{0}};
  ui_layout_ask(board, 2, r);
  strcpy(m.ask.options[0].label, "Allow");
  m.ask.options[0].style = UI_STYLE_ALLOW;
  m.ask.options[0].rect = r[0];
  strcpy(m.ask.options[1].label, "Deny");
  m.ask.options[1].style = UI_STYLE_DENY;
  m.ask.options[1].rect = r[1];
  m.ask.answerable = true;
  m.ask.chosen = 0;
  shot("fx_ask_chosen");
}

static void test_battery_low_and_charging(void) {
  m.screen = UI_SCREEN_IDLE;
  m.maus = UI_MAUS_IDLE;
  m.battery.pct = 8;
  m.battery.charging = true;
  shot("fx_battery_charging");
}

int main(int argc, char **argv) {
  if (argc != 2 || !(board = gadget_board_by_id(argv[1]))) {
    fprintf(stderr, "usage: test_ui_snapshots <board-id>  (run from the repository root)\n");
    return 2;
  }
  snprintf(dir, sizeof dir, "firmware/tests/snapshots/%s", board->id);
  UNITY_BEGIN();
  RUN_TEST(test_thinking);
  RUN_TEST(test_thinking_working);
  RUN_TEST(test_speaking_1);
  RUN_TEST(test_speaking_2);
  RUN_TEST(test_speaking_3);
  RUN_TEST(test_speaking_second_page);
  RUN_TEST(test_reply);
  RUN_TEST(test_reply_failed);
  RUN_TEST(test_setup_need_wifi);
  RUN_TEST(test_setup_host_not_found);
  RUN_TEST(test_offline_wifi_connecting);
  RUN_TEST(test_offline_wifi_failed);
  RUN_TEST(test_offline_looking);
  RUN_TEST(test_offline_protocol);
  RUN_TEST(test_update_verifying);
  RUN_TEST(test_update_restarting);
  RUN_TEST(test_ask_answered_here);
  RUN_TEST(test_battery_low_and_charging);
  return UNITY_END();
}
