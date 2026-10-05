/* SPDX-License-Identifier: Apache-2.0 */
/* Display helpers that do not need LVGL or ESP-IDF. */
#ifndef PL_DISPLAY_H
#define PL_DISPLAY_H

#include <stdint.h>

/* CO5300 panels accept only areas that start on an even column/row and end
 * on an odd one. Widens the inclusive area [x1..x2] x [y1..y2] to that rule
 * and keeps it inside a w x h screen (w and h even). */
void pl_round_even_area(int32_t *x1, int32_t *y1, int32_t *x2, int32_t *y2, int32_t w, int32_t h);

#endif /* PL_DISPLAY_H */
