/* SPDX-License-Identifier: Apache-2.0 */
/* SDL window mode (P2b): the LVGL SDL window plus an event filter that turns
 * Space, Esc and the mouse into HAL input events. Private to ports/sim. */
#ifndef SIM_SDL_H
#define SIM_SDL_H

#include <stdbool.h>
#include "gadget_board.h"
#include "lvgl.h"

/* lv_sdl_window_create + title + zoom, then SDL_SetEventFilter. NULL on failure. */
lv_display_t *sim_sdl_window_create(const gadget_board_t *board, float zoom);
/* SDL_PumpEvents(): runs the filter for everything queued since the last call. */
void sim_sdl_pump(void);
bool sim_sdl_quit_requested(void);
/* Delete the window's display, then SDL_Quit (lv_sdl_quit). */
void sim_sdl_shutdown(lv_display_t *disp);

#endif /* SIM_SDL_H */
