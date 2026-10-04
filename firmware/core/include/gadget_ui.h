/* firmware/core/include/gadget_ui.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* The UI API. Implemented by firmware/ui (LVGL, P2b) and by
 * ports/sim/ui_stub.c (no LVGL, P2a). Called by the port's main loop only:
 *
 *   core_tick(now); ui_render(core_ui_model()); ui_tick(now);
 *
 * Layout and hit-testing are pure functions implemented in core
 * (core/src/ui_layout.c) so core can hit-test touches without LVGL and the
 * UI places its buttons on exactly the same rectangles. */
#ifndef GADGET_UI_H
#define GADGET_UI_H

#include "gadget_board.h"
#include "gadget_ui_model.h"

/* Port has already called lv_init() and created the display (and, for
 * touch, the pointer indev). Builds every screen on lv_screen_active() and
 * sets the Latin-1 fonts through styles on the UI root. */
gadget_status_t ui_init(const gadget_board_t *board, uint32_t prng_seed);
/* Apply the model. Cheap when m->rev is unchanged. Never calls into core. */
void ui_render(const ui_model_t *m);
/* Advance the Maus animation (expressions, blinks, bob/jitter, mouth) to
 * now_ms, then call lv_timer_handler() exactly once. */
void ui_tick(uint64_t now_ms);
void ui_deinit(void);

/* ---- Implemented in core (core/src/ui_layout.c) --------------------------- */
/* Button rectangles for an ask with n options (1..4) on a touch board,
 * inside the round safe area when board->screen_round. */
void ui_layout_ask(const gadget_board_t *board, uint8_t n_options, gadget_rect_t out[UI_ASK_OPTIONS_MAX]);
/* Option index (0..n-1) under (x, y) on the ask screen, or -1. */
int ui_hit_test(const ui_model_t *m, int16_t x, int16_t y);
/* The text-safe rectangle (inscribed square on round screens, minus margins). */
gadget_rect_t ui_safe_area(const gadget_board_t *board);

#endif /* GADGET_UI_H */
