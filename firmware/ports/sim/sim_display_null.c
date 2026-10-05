/* firmware/ports/sim/sim_display_null.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The display without LVGL (GADGET_WITH_LVGL=OFF): nothing is drawn and
 * snapshots are skipped. Plan P2b adds sim_display_lvgl.c. */
#include "sim_display.h"

int sim_display_init(const gadget_board_t *board, const sim_display_opts_t *opts) {
  (void)board;
  (void)opts;
  return 0;
}

void sim_display_advance(uint32_t elapsed_ms) { (void)elapsed_ms; }
void sim_display_poll(void) {}
void sim_display_touch(bool pressed, int16_t x, int16_t y) {
  (void)pressed;
  (void)x;
  (void)y;
}
bool sim_display_quit_requested(void) { return false; }
int sim_display_snapshot(const char *name) {
  (void)name;
  return -1;
}
void sim_display_deinit(void) {}
