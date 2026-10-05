/* firmware/ports/sim/ui_stub.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The UI API without LVGL: remembers the last rendered revision and screen,
 * and with --trace prints screen changes. Plan P2b's firmware/ui replaces it. */
#include <stdio.h>
#include "gadget_ui.h"
#include "sim_internal.h"

static const char *const SCREEN_NAMES[UI_SCREEN__COUNT] = {
    "boot", "setup", "offline", "idle", "listening", "thinking", "speaking", "reply", "ask", "card", "image", "update"};

static uint32_t s_rev;
static int s_screen = -1;

gadget_status_t ui_init(const gadget_board_t *board, uint32_t prng_seed) {
  (void)board;
  (void)prng_seed;
  s_rev = 0;
  s_screen = -1;
  return GADGET_OK;
}

void ui_render(const ui_model_t *m) {
  if (m->rev == s_rev) return;
  s_rev = m->rev;
  if ((int)m->screen != s_screen) {
    s_screen = (int)m->screen;
    if (g_sim.trace && m->screen < UI_SCREEN__COUNT) fprintf(stderr, "ui: %s\n", SCREEN_NAMES[m->screen]);
  }
}

void ui_tick(uint64_t now_ms) { (void)now_ms; }
void ui_deinit(void) {}
