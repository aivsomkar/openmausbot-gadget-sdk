/* SPDX-License-Identifier: Apache-2.0 */
#include "ui_maus_engine.h"

#include <string.h>

/* sin(i * pi / 128) * 32767 for i = 0..64 (a quarter turn in 64 steps). */
static const int16_t k_sin_q15[65] = {
  0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512,
  10278, 11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868,
  19519, 20159, 20787, 21403, 22005, 22594, 23170, 23731, 24279, 24811, 25329, 25832, 26319,
  26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956, 30273, 30571, 30852, 31113,
  31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757, 32767,
};

static int32_t sin_at(uint32_t i) { /* i in 1/256 turns */
  i &= 255u;
  if (i <= 64u) return k_sin_q15[i];
  if (i <= 128u) return k_sin_q15[128u - i];
  if (i <= 192u) return -k_sin_q15[i - 128u];
  return -k_sin_q15[256u - i];
}

int32_t maus_sin_q15(uint32_t phase16) {
  uint32_t i = (phase16 & 0xFFFFu) >> 8;
  int32_t f = (int32_t)(phase16 & 0xFFu);
  int32_t a = sin_at(i);
  int32_t b = sin_at(i + 1u);
  return a + (b - a) * f / 256;
}

uint8_t maus_blink_step(uint32_t t_ms) {
  int32_t s; /* eye openness x1000: fast close, slower open, never below 0.04 */
  if (t_ms < MAUS_BLINK_CLOSE_MS) s = 1000 - (int32_t)(t_ms * 1000u / MAUS_BLINK_CLOSE_MS);
  else s = (int32_t)((t_ms - MAUS_BLINK_CLOSE_MS) * 1000u / (MAUS_BLINK_MS - MAUS_BLINK_CLOSE_MS));
  if (s < 40) s = 40;
  /* nearest baked step: 1.0, 0.6, 0.25, 0.04 */
  if (s > 800) return 0;
  if (s > 425) return 1;
  if (s > 145) return 2;
  return 3;
}

static uint32_t phase16(uint64_t t_ms, uint32_t period_ms) {
  return (uint32_t)((t_ms % period_ms) * 65536u / period_ms);
}

static int16_t tenths_to_px(int32_t v) {
  return (int16_t)(v >= 0 ? (v + 5) / 10 : -((-v + 5) / 10));
}

static uint64_t after(maus_engine_t *e, uint64_t now, uint16_t lo, uint16_t hi) {
  return now + gadget_prng_range(&e->prng, lo, hi);
}

static void start_blink(maus_engine_t *e, uint64_t now, int16_t pending) {
  e->blinking = true;
  e->blink_start = now;
  e->pending = pending;
}

void maus_engine_init(maus_engine_t *e, const maus_art_t *art, uint32_t seed) {
  memset(e, 0, sizeof *e);
  e->art = art;
  gadget_prng_seed(&e->prng, seed);
  e->state = UI_MAUS_NONE;
  e->pending = -1;
}

void maus_engine_set_state(maus_engine_t *e, ui_maus_state_t state, uint64_t now_ms) {
  if (state == e->state) return;
  ui_maus_state_t prev = e->state;
  e->state = state;
  e->since_ms = now_ms;
  if (state == UI_MAUS_NONE) return;
  const maus_state_def_t *d = &e->art->states[state];
  uint8_t first = d->pool[0];
  if (prev == UI_MAUS_NONE) {
    e->expr = first;
    e->blinking = false;
    e->pending = -1;
  } else if (e->expr != first) {
    start_blink(e, now_ms, (int16_t)first);
  }
  e->next_expr = after(e, now_ms, d->cad_min_ms, d->cad_max_ms);
  e->next_blink = d->blink_max_ms ? after(e, now_ms, d->blink_min_ms, d->blink_max_ms) : 0;
}

static uint8_t pick_other(maus_engine_t *e, const maus_state_def_t *d) {
  uint8_t cand[16];
  uint8_t n = 0;
  for (uint8_t i = 0; i < d->pool_len && n < sizeof cand; i++) {
    if (d->pool[i] != e->expr) cand[n++] = d->pool[i];
  }
  if (n == 0) return e->expr;
  return cand[gadget_prng_range(&e->prng, 0, (uint32_t)n - 1u)];
}

maus_frame_t maus_engine_step(maus_engine_t *e, uint64_t now, uint8_t speak_level) {
  maus_frame_t f = {0, 0, -1, 0, 0, 0};
  if (e->state == UI_MAUS_NONE) return f;
  const maus_state_def_t *d = &e->art->states[e->state];

  if (now >= e->next_expr) {
    if (d->pool_len > 1 && !e->blinking) start_blink(e, now, (int16_t)pick_other(e, d));
    e->next_expr = after(e, now, d->cad_min_ms, d->cad_max_ms);
  }
  if (!e->blinking && e->next_blink != 0 && now >= e->next_blink) {
    start_blink(e, now, -1);
    e->next_blink = after(e, now, d->blink_min_ms, d->blink_max_ms);
  }

  if (e->blinking) {
    uint64_t t = now - e->blink_start;
    if (t >= MAUS_BLINK_MS) {
      e->blinking = false;
      if (e->pending >= 0) e->expr = (uint8_t)e->pending; /* a tick skipped the closed step */
      e->pending = -1;
    } else {
      f.blink_step = maus_blink_step((uint32_t)t);
      if (e->pending >= 0 && f.blink_step == MAUS_BLINK_STEPS - 1) {
        e->expr = (uint8_t)e->pending;
        e->pending = -1;
      }
    }
  }
  f.expr = e->expr;

  if (e->state == UI_MAUS_SPEAKING && speak_level > 0) {
    for (uint8_t i = 0; i < MAUS_SPEAK_EXPRS; i++) {
      if (e->art->speak_expr[i] == e->expr) {
        f.speak = (int8_t)i;
        f.speak_level = speak_level > MAUS_SPEAK_LEVELS ? MAUS_SPEAK_LEVELS : speak_level;
      }
    }
  }

  uint64_t t = now - e->since_ms;
  int32_t dx = 0;
  int32_t dy = 0;
  if (d->bob_ms) dy -= d->bob_px_x10 * maus_sin_q15(phase16(t, d->bob_ms)) / 32767;
  if (d->jitter_ms) {
    uint32_t py = (uint32_t)d->jitter_ms * 63u / 100u;
    dx += d->jitter_px_x10 * maus_sin_q15(phase16(t, d->jitter_ms)) / 32767;
    dy += d->jitter_px_x10 * maus_sin_q15(phase16(t, py ? py : 1u) + 11473u) / 32767; /* +1.1 rad */
  }
  if (d->circle_ms) {
    uint32_t p = phase16(t, d->circle_ms);
    dx += d->circle_px_x10 * maus_sin_q15(p) / 32767;
    dy += d->circle_px_x10 * maus_sin_q15(p + 16384u) / 32767; /* +pi/2 */
  }
  f.dx = tenths_to_px(dx);
  f.dy = tenths_to_px(dy);
  return f;
}
