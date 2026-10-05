/* firmware/ports/sim/sim_display.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Simulator display seam. P2a ships sim_display_null.c (no LVGL); P2b ships
 * sim_display_lvgl.c (LVGL test display for --headless, SDL window
 * otherwise, round mask, the shared touch pointer indev). Exactly one is
 * linked, chosen by GADGET_WITH_LVGL. */
#ifndef SIM_DISPLAY_H
#define SIM_DISPLAY_H

#include "gadget_board.h"

typedef struct {
  bool headless;
  float zoom;              /* window mode only */
  const char *snapshot_dir;
} sim_display_opts_t;

/* lv_init() + display + indev (+ SDL init and event filter in window mode). */
int sim_display_init(const gadget_board_t *board, const sim_display_opts_t *opts);
/* Virtual-clock mode: lv_tick_inc(elapsed_ms). Window mode: no-op. */
void sim_display_advance(uint32_t elapsed_ms);
/* Pump SDL events (window mode); the event filter turns them into HAL
 * events with sim_post_event() (sim_hal.h). No-op when headless. */
void sim_display_poll(void);
/* The script's touch commands drive the same pointer state as the mouse. */
void sim_display_touch(bool pressed, int16_t x, int16_t y);
/* Window closed or SDL quit requested. */
bool sim_display_quit_requested(void);
/* `snapshot <name>`: 1 = passed, 0 = failed (writes <name>_err.png),
 * 2 = no reference, -1 = not supported in this build (null display).
 * Built with GADGET_SNAPSHOT_UPDATE: unlink("<snapshot-dir>/<name>.png")
 * first, so LVGL (which only creates a missing reference) writes it anew. */
int sim_display_snapshot(const char *name);
/* SDL_Quit / lv_deinit; must run before every execv restart. */
void sim_display_deinit(void);

#endif /* SIM_DISPLAY_H */
