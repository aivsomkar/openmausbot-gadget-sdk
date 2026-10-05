/* SPDX-License-Identifier: Apache-2.0 */
/* Button debouncing and touch edge detection. hal_input.c polls the board
 * every 10 ms and turns the results into GADGET_EV_INPUT events; core
 * derives holds, taps and swipes from them (gadget_hal.h input group). */
#ifndef PL_INPUT_H
#define PL_INPUT_H

#include "gadget_events.h"

#define PL_BUTTON_STABLE_POLLS 2u   /* 20 ms at the 10 ms poll */
#define PL_TOUCH_MOVE_MIN_PX 2

typedef struct {
  bool pressed;   /* debounced state */
  bool raw;       /* last raw sample */
  uint8_t same;   /* consecutive polls with this raw value */
  bool armed;     /* false while a press that began before boot is still held */
} pl_button_t;

/* raw_now: the level at boot. A button already held at boot (for example
 * the PWR press that switched the board on) reports nothing until it has
 * been released once. */
void pl_button_init(pl_button_t *b, bool raw_now);
/* Returns +1 on a debounced press, -1 on a debounced release, else 0. */
int pl_button_poll(pl_button_t *b, bool raw);

typedef struct {
  bool down;
  int16_t x, y;
} pl_touch_t;

void pl_touch_init(pl_touch_t *t);
/* One poll of the touch controller. Writes at most one event (TOUCH_DOWN,
 * TOUCH_MOVE when the point moved by >= PL_TOUCH_MOVE_MIN_PX, TOUCH_UP at
 * the last point) and returns 1, else returns 0. Coordinates are clamped
 * to the w x h screen. */
int pl_touch_poll(pl_touch_t *t, bool pressed, int32_t x, int32_t y, uint16_t w, uint16_t h, gadget_input_t *out);

#endif /* PL_INPUT_H */
