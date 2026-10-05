/* SPDX-License-Identifier: Apache-2.0 */
/* Window mode input through the real SDL event filter, on SDL's dummy video
 * driver (CTest sets SDL_VIDEODRIVER=dummy). This test supplies its own
 * sim_post_event to record what the filter queues. */
#include <string.h>
#include <SDL2/SDL.h>

#include "gadget_board.h"
#include "lvgl.h"
#include "sim_display.h"
#include "sim_hal.h"
#include "unity.h"

static gadget_event_t posted[16];
static int n_posted;

void sim_post_event(const gadget_event_t *ev) {
  if (n_posted < 16) posted[n_posted++] = *ev;
}

static bool g_open;

static void open_window(const char *board_id, float zoom) {
  const gadget_board_t *b = gadget_board_by_id(board_id);
  TEST_ASSERT_NOT_NULL(b);
  sim_display_opts_t o = {false, zoom, "."};
  TEST_ASSERT_EQUAL_INT(0, sim_display_init(b, &o));
  g_open = true;
}

static void close_window(void) {
  sim_display_deinit();
  g_open = false;
}

void setUp(void) { n_posted = 0; }
void tearDown(void) {
  if (g_open) close_window();
}

static void key(SDL_EventType type, SDL_Keycode sym, Uint8 repeat) {
  SDL_Event e;
  SDL_zero(e);
  e.type = type;
  e.key.keysym.sym = sym;
  e.key.repeat = repeat;
  TEST_ASSERT_EQUAL_INT(0, SDL_PushEvent(&e)); /* 0: the filter consumed it */
}

static void mouse_button(SDL_EventType type, int x, int y) {
  SDL_Event e;
  SDL_zero(e);
  e.type = type;
  e.button.button = SDL_BUTTON_LEFT;
  e.button.x = x;
  e.button.y = y;
  TEST_ASSERT_EQUAL_INT(0, SDL_PushEvent(&e));
}

static void test_space_is_talk_and_repeat_is_ignored(void) {
  open_window("amoled-175c", 1.0f);
  key(SDL_KEYDOWN, SDLK_SPACE, 0);
  key(SDL_KEYDOWN, SDLK_SPACE, 1);
  key(SDL_KEYUP, SDLK_SPACE, 0);
  TEST_ASSERT_EQUAL_INT(2, n_posted);
  TEST_ASSERT_EQUAL_INT(GADGET_EV_INPUT, posted[0].type);
  TEST_ASSERT_EQUAL_INT(GADGET_IN_TALK_DOWN, posted[0].u.input.type);
  TEST_ASSERT_EQUAL_INT(GADGET_IN_TALK_UP, posted[1].u.input.type);
}

static void test_escape_is_cancel_only_on_boards_with_cancel(void) {
  open_window("amoled-175c", 1.0f);
  key(SDL_KEYDOWN, SDLK_ESCAPE, 0);
  key(SDL_KEYUP, SDLK_ESCAPE, 0);
  TEST_ASSERT_EQUAL_INT(2, n_posted);
  TEST_ASSERT_EQUAL_INT(GADGET_IN_CANCEL_DOWN, posted[0].u.input.type);
  TEST_ASSERT_EQUAL_INT(GADGET_IN_CANCEL_UP, posted[1].u.input.type);
  close_window();
  n_posted = 0;
  open_window("amoled-175", 1.0f); /* touch + talk only */
  key(SDL_KEYDOWN, SDLK_ESCAPE, 0);
  TEST_ASSERT_EQUAL_INT(0, n_posted);
}

static void test_mouse_is_touch_in_screen_pixels(void) {
  open_window("amoled-175c", 2.0f);
  mouse_button(SDL_MOUSEBUTTONDOWN, 200, 120);
  SDL_Event e;
  SDL_zero(e);
  e.type = SDL_MOUSEMOTION;
  e.motion.x = 220;
  e.motion.y = 140;
  TEST_ASSERT_EQUAL_INT(0, SDL_PushEvent(&e));
  mouse_button(SDL_MOUSEBUTTONUP, 2000, 2000); /* outside: clamped to the screen */
  TEST_ASSERT_EQUAL_INT(3, n_posted);
  TEST_ASSERT_EQUAL_INT(GADGET_IN_TOUCH_DOWN, posted[0].u.input.type);
  TEST_ASSERT_EQUAL_INT16(100, posted[0].u.input.x);
  TEST_ASSERT_EQUAL_INT16(60, posted[0].u.input.y);
  TEST_ASSERT_EQUAL_INT(GADGET_IN_TOUCH_MOVE, posted[1].u.input.type);
  TEST_ASSERT_EQUAL_INT16(110, posted[1].u.input.x);
  TEST_ASSERT_EQUAL_INT(GADGET_IN_TOUCH_UP, posted[2].u.input.type);
  TEST_ASSERT_EQUAL_INT16(465, posted[2].u.input.x);
  TEST_ASSERT_EQUAL_INT16(465, posted[2].u.input.y);
}

static void test_mouse_does_nothing_on_button_boards(void) {
  open_window("lcd-154", 2.0f);
  mouse_button(SDL_MOUSEBUTTONDOWN, 10, 10);
  TEST_ASSERT_EQUAL_INT(0, n_posted);
}

static void test_quit_and_close_are_dropped_but_remembered(void) {
  open_window("devkit", 2.0f);
  TEST_ASSERT_FALSE(sim_display_quit_requested());
  SDL_Event e;
  SDL_zero(e);
  e.type = SDL_WINDOWEVENT;
  e.window.event = SDL_WINDOWEVENT_CLOSE;
  TEST_ASSERT_EQUAL_INT(0, SDL_PushEvent(&e));
  TEST_ASSERT_TRUE(sim_display_quit_requested());
  SDL_zero(e);
  e.type = SDL_QUIT;
  TEST_ASSERT_EQUAL_INT(0, SDL_PushEvent(&e));
  sim_display_poll();
  lv_timer_handler(); /* LVGL's SDL timer must not exit or delete the display */
  TEST_ASSERT_NOT_NULL(lv_display_get_default());
  /* main.c runs sim_display_deinit() before every execv restart: SDL must be fully shut */
  close_window();
  TEST_ASSERT_EQUAL_UINT32(0, SDL_WasInit(SDL_INIT_EVERYTHING));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_space_is_talk_and_repeat_is_ignored);
  RUN_TEST(test_escape_is_cancel_only_on_boards_with_cancel);
  RUN_TEST(test_mouse_is_touch_in_screen_pixels);
  RUN_TEST(test_mouse_does_nothing_on_button_boards);
  RUN_TEST(test_quit_and_close_are_dropped_but_remembered);
  return UNITY_END();
}
