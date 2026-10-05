/* SPDX-License-Identifier: Apache-2.0 */
/* Window mode input (spec 5.7, contract 2.16): Space = TALK, Esc = CANCEL,
 * mouse = touch, key repeat ignored; quit and window-close are dropped so
 * LVGL never exits or deletes the display (LV_SDL_DIRECT_EXIT is 0). The
 * filter only queues events (sim_post_event is thread-safe) and updates
 * the shared pointer state; it never calls into core or LVGL. */
#include "sim_sdl.h"

#include <stdatomic.h>
#include <stdio.h>
#include <SDL2/SDL.h>

#include "sim_display.h"
#include "sim_hal.h"

static const gadget_board_t *s_board;
static float s_zoom = 1.0f;
static atomic_bool s_quit;
static bool s_mouse_down;

static void post_input(gadget_input_type_t type, int16_t x, int16_t y) {
  gadget_event_t ev;
  ev.type = GADGET_EV_INPUT;
  ev.u.input.type = type;
  ev.u.input.x = x;
  ev.u.input.y = y;
  ev.u.input.dir = GADGET_SWIPE_UP;
  sim_post_event(&ev);
}

static int16_t to_screen(int32_t v, uint16_t limit) {
  int32_t p = (int32_t)((float)v / s_zoom);
  if (p < 0) p = 0;
  if (p >= (int32_t)limit) p = (int32_t)limit - 1;
  return (int16_t)p;
}

static int SDLCALL filter(void *userdata, SDL_Event *e) {
  (void)userdata;
  const uint32_t inputs = s_board->input_mask;
  switch (e->type) {
  case SDL_KEYDOWN:
  case SDL_KEYUP: {
    const bool down = e->type == SDL_KEYDOWN;
    if (e->key.keysym.sym == SDLK_SPACE) {
      if (!e->key.repeat && (inputs & GADGET_INPUT_TALK)) post_input(down ? GADGET_IN_TALK_DOWN : GADGET_IN_TALK_UP, 0, 0);
      return 0;
    }
    if (e->key.keysym.sym == SDLK_ESCAPE) {
      if (!e->key.repeat && (inputs & GADGET_INPUT_CANCEL)) post_input(down ? GADGET_IN_CANCEL_DOWN : GADGET_IN_CANCEL_UP, 0, 0);
      return 0;
    }
    return 1;
  }
  case SDL_TEXTINPUT:
    return 0;
  case SDL_MOUSEBUTTONDOWN:
  case SDL_MOUSEBUTTONUP: {
    if (e->button.button != SDL_BUTTON_LEFT) return 0;
    if (!(inputs & GADGET_INPUT_TOUCH)) return 0;
    const bool down = e->type == SDL_MOUSEBUTTONDOWN;
    int16_t x = to_screen(e->button.x, s_board->screen_w);
    int16_t y = to_screen(e->button.y, s_board->screen_h);
    s_mouse_down = down;
    sim_display_touch(down, x, y);
    post_input(down ? GADGET_IN_TOUCH_DOWN : GADGET_IN_TOUCH_UP, x, y);
    return 0;
  }
  case SDL_MOUSEMOTION: {
    if (s_mouse_down && (inputs & GADGET_INPUT_TOUCH)) {
      int16_t x = to_screen(e->motion.x, s_board->screen_w);
      int16_t y = to_screen(e->motion.y, s_board->screen_h);
      sim_display_touch(true, x, y);
      post_input(GADGET_IN_TOUCH_MOVE, x, y);
    }
    return 0;
  }
  case SDL_QUIT:
    atomic_store(&s_quit, true);
    return 0;
  case SDL_WINDOWEVENT:
    if (e->window.event == SDL_WINDOWEVENT_CLOSE) {
      atomic_store(&s_quit, true);
      return 0;
    }
    return 1;
  default:
    return 1;
  }
}

lv_display_t *sim_sdl_window_create(const gadget_board_t *board, float zoom) {
  s_board = board;
  s_zoom = zoom > 0.0f ? zoom : 1.0f;
  atomic_store(&s_quit, false);
  s_mouse_down = false;
  lv_display_t *disp = lv_sdl_window_create(board->screen_w, board->screen_h);
  if (!disp) {
    fprintf(stderr, "gadget-sim: cannot open a window: %s\n", SDL_GetError());
    return NULL;
  }
  char title[64];
  snprintf(title, sizeof title, "gadget-sim %s", board->id);
  lv_sdl_window_set_title(disp, title);
  lv_sdl_window_set_zoom(disp, s_zoom);
  SDL_SetEventFilter(filter, NULL);
  return disp;
}

void sim_sdl_pump(void) { SDL_PumpEvents(); }

bool sim_sdl_quit_requested(void) { return atomic_load(&s_quit); }

void sim_sdl_shutdown(lv_display_t *disp) {
  SDL_SetEventFilter(NULL, NULL);
  if (disp) lv_display_delete(disp);
  lv_sdl_quit();
}
