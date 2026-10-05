/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_audio.h"

#include <string.h>

void pl_ring_init(pl_ring_t *r, int16_t *storage, size_t cap) {
  r->buf = storage;
  r->cap = cap;
  r->head = 0;
  r->count = 0;
}

size_t pl_ring_write(pl_ring_t *r, const int16_t *pcm, size_t n) {
  size_t room = r->cap - r->count;
  if (n > room) {
    n = room;
  }
  size_t tail = (r->head + r->count) % r->cap;
  size_t first = r->cap - tail < n ? r->cap - tail : n;
  memcpy(r->buf + tail, pcm, first * sizeof(int16_t));
  memcpy(r->buf, pcm + first, (n - first) * sizeof(int16_t));
  r->count += n;
  return n;
}

size_t pl_ring_read(pl_ring_t *r, int16_t *out, size_t n) {
  if (n > r->count) {
    n = r->count;
  }
  size_t first = r->cap - r->head < n ? r->cap - r->head : n;
  memcpy(out, r->buf + r->head, first * sizeof(int16_t));
  memcpy(out + first, r->buf, (n - first) * sizeof(int16_t));
  r->head = (r->head + n) % r->cap;
  r->count -= n;
  return n;
}

size_t pl_ring_count(const pl_ring_t *r) { return r->count; }

void pl_ring_clear(pl_ring_t *r) {
  r->head = 0;
  r->count = 0;
}

uint32_t pl_samples_to_ms(size_t samples, uint32_t rate) {
  if (rate == 0) {
    return 0;
  }
  return (uint32_t)(((uint64_t)samples * 1000u) / rate);
}

void pl_pcm_from_i32(const int32_t *in, int16_t *out, size_t n, unsigned shift) {
  for (size_t i = 0; i < n; i++) {
    int32_t v = in[i] >> shift;
    if (v > INT16_MAX) {
      v = INT16_MAX;
    } else if (v < INT16_MIN) {
      v = INT16_MIN;
    }
    out[i] = (int16_t)v;
  }
}

void pl_pcm_volume(int16_t *pcm, size_t n, uint8_t pct) {
  if (pct >= 100) {
    return;
  }
  int32_t gain = (int32_t)pct * (int32_t)pct * 32768 / 10000; /* Q15 */
  for (size_t i = 0; i < n; i++) {
    pcm[i] = (int16_t)(((int32_t)pcm[i] * gain) >> 15);
  }
}
