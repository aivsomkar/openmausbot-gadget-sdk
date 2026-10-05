/* SPDX-License-Identifier: Apache-2.0 */
/* Audio helpers for the ESP32 port: the speaker ring buffer (not locked;
 * hal_audio.c holds a mutex around every call), sample conversion and
 * software volume. */
#ifndef PL_AUDIO_H
#define PL_AUDIO_H

#include "gadget_types.h"

typedef struct {
  int16_t *buf;
  size_t cap;
  size_t head;    /* next read index */
  size_t count;   /* samples stored */
} pl_ring_t;

void pl_ring_init(pl_ring_t *r, int16_t *storage, size_t cap);
/* Store as many of n samples as fit; returns how many were stored. */
size_t pl_ring_write(pl_ring_t *r, const int16_t *pcm, size_t n);
/* Take up to n samples; returns how many were taken. */
size_t pl_ring_read(pl_ring_t *r, int16_t *out, size_t n);
size_t pl_ring_count(const pl_ring_t *r);
void pl_ring_clear(pl_ring_t *r);

/* samples at rate Hz → whole milliseconds (rounded down). */
uint32_t pl_samples_to_ms(size_t samples, uint32_t rate);
/* 32-bit I2S samples (24-bit microphones put data in the top bits) to
 * PCM16: arithmetic shift right by `shift`, saturated. */
void pl_pcm_from_i32(const int32_t *in, int16_t *out, size_t n, unsigned shift);
/* Software volume, in place: gain = (pct/100)^2, so 50 % is -12 dB. */
void pl_pcm_volume(int16_t *pcm, size_t n, uint8_t pct);

#endif /* PL_AUDIO_H */
