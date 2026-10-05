/* SPDX-License-Identifier: Apache-2.0 */
/* The Maus animation as plain state: which expression, which blink step,
 * which mouth and how far the body is moved, for a time `now`. No LVGL
 * objects; ui_maus.c turns a frame into image sources and positions.
 * Integer-only and driven by the seeded xorshift32 PRNG (gadget_util.h),
 * so a fixed seed gives the same frames on every machine (spec 5.5). */
#ifndef UI_MAUS_ENGINE_H
#define UI_MAUS_ENGINE_H

#include "art/maus_art.h"
#include "gadget_util.h"

#define MAUS_BLINK_MS 320u
#define MAUS_BLINK_CLOSE_MS 134u /* the eye is fully closed at 0.42 of the blink */

typedef struct {
  uint8_t expr;        /* index into art->expr_ids */
  uint8_t blink_step;  /* 0 (open) .. MAUS_BLINK_STEPS-1 (closed) */
  int8_t speak;        /* index into art->speak_expr when an open mouth shows, else -1 */
  uint8_t speak_level; /* 1..MAUS_SPEAK_LEVELS when speak >= 0, else 0 */
  int16_t dx, dy;      /* body offset in px */
} maus_frame_t;

typedef struct {
  const maus_art_t *art;
  gadget_prng_t prng;
  ui_maus_state_t state; /* UI_MAUS_NONE: hidden */
  uint64_t since_ms;     /* start of the current state (motion phase 0) */
  uint8_t expr;
  int16_t pending;       /* expression to switch to at the blink's closed step, -1 = none */
  bool blinking;
  uint64_t blink_start;
  uint64_t next_blink;   /* 0 = this state never blinks on its own */
  uint64_t next_expr;
} maus_engine_t;

void maus_engine_init(maus_engine_t *e, const maus_art_t *art, uint32_t seed);
/* Enter a state. Coming from UI_MAUS_NONE shows the pool's first expression
 * at once; otherwise a different first expression arrives under a blink. */
void maus_engine_set_state(maus_engine_t *e, ui_maus_state_t state, uint64_t now_ms);
/* Advance to now_ms (monotonic) and return the frame to draw. */
maus_frame_t maus_engine_step(maus_engine_t *e, uint64_t now_ms, uint8_t speak_level);

/* Helpers, exposed for tests. */
int32_t maus_sin_q15(uint32_t phase16);           /* sin(2*pi*phase16/65536) * 32767 */
uint8_t maus_blink_step(uint32_t t_ms);           /* blink step at t ms into a blink */

#endif /* UI_MAUS_ENGINE_H */
