/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_display.h"

void pl_round_even_area(int32_t *x1, int32_t *y1, int32_t *x2, int32_t *y2, int32_t w, int32_t h) {
  *x1 &= ~(int32_t)1;
  *y1 &= ~(int32_t)1;
  *x2 |= 1;
  *y2 |= 1;
  if (*x2 > w - 1) {
    *x2 = w - 1;
  }
  if (*y2 > h - 1) {
    *y2 = h - 1;
  }
}
