/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_input.h"

#include <string.h>

void pl_button_init(pl_button_t *b, bool raw_now) {
  b->pressed = false;
  b->raw = raw_now;
  b->same = PL_BUTTON_STABLE_POLLS;
  b->armed = !raw_now;
}

int pl_button_poll(pl_button_t *b, bool raw) {
  if (raw == b->raw) {
    if (b->same < 255) {
      b->same++;
    }
  } else {
    b->raw = raw;
    b->same = 1;
  }
  if (b->same < PL_BUTTON_STABLE_POLLS) {
    return 0;
  }
  if (!b->armed) {
    if (!raw) {
      b->armed = true;
    }
    return 0;
  }
  if (raw != b->pressed) {
    b->pressed = raw;
    return raw ? 1 : -1;
  }
  return 0;
}

void pl_touch_init(pl_touch_t *t) { memset(t, 0, sizeof(*t)); }

static int16_t clamp(int32_t v, uint16_t size) {
  if (v < 0) {
    return 0;
  }
  if (size > 0 && v > (int32_t)size - 1) {
    return (int16_t)(size - 1);
  }
  return (int16_t)v;
}

int pl_touch_poll(pl_touch_t *t, bool pressed, int32_t x, int32_t y, uint16_t w, uint16_t h, gadget_input_t *out) {
  memset(out, 0, sizeof(*out));
  if (!pressed) {
    if (!t->down) {
      return 0;
    }
    t->down = false;
    out->type = GADGET_IN_TOUCH_UP;
    out->x = t->x;
    out->y = t->y;
    return 1;
  }
  int16_t cx = clamp(x, w);
  int16_t cy = clamp(y, h);
  if (!t->down) {
    t->down = true;
    t->x = cx;
    t->y = cy;
    out->type = GADGET_IN_TOUCH_DOWN;
    out->x = cx;
    out->y = cy;
    return 1;
  }
  int dx = cx > t->x ? cx - t->x : t->x - cx;
  int dy = cy > t->y ? cy - t->y : t->y - cy;
  if (dx + dy < PL_TOUCH_MOVE_MIN_PX) {
    return 0;
  }
  t->x = cx;
  t->y = cy;
  out->type = GADGET_IN_TOUCH_MOVE;
  out->x = cx;
  out->y = cy;
  return 1;
}
