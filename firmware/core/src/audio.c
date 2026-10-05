/* firmware/core/src/audio.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Speech playback (spec §4.4): speak.begin / frames / speak.end / speak.stop
 * into a 1 s jitter buffer that feeds hal_spk_write in 20 ms blocks, and the
 * mouth level the UI draws (contract §2.7 "Speaking level"). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_internal.h"

#define TAG "audio"
#define PREBUFFER_MS 200u
#define LEVEL_QUEUE 64u
#define LEVEL_DROP_MS 60u

/* Mean-square thresholds for -42, -32 and -24 dBFS (32768^2 * 10^(dB/10)). */
#define MS_42DB 67749u
#define MS_32DB 677485u
#define MS_24DB 4274643u

static struct {
  bool active;
  uint8_t stream;
  uint32_t rate;
  char turn[GADGET_TURN_MAX + 1];  /* "" for speech that belongs to no turn (posts) */
  int16_t *ring;
  size_t cap, head, count;         /* samples */
  bool ended;                      /* speak.end arrived */
  bool playing;                    /* prebuffer reached: feeding the HAL */
  bool overflow_logged;
  uint64_t received, written;      /* samples */
  struct {
    uint64_t at;
    uint8_t level;
  } lv[LEVEL_QUEUE];
  size_t lv_head, lv_n;
  uint8_t level;
  uint64_t level_changed;
} A;

static uint8_t level_of(const int16_t *pcm, size_t n) {
  uint32_t ms = core_mean_square(pcm, n);
  if (ms < MS_42DB) return 0;
  if (ms < MS_32DB) return 1;
  if (ms < MS_24DB) return 2;
  return 3;
}

static void release(void) {
  free(A.ring);
  A.ring = NULL;
  A.active = false;
  A.playing = false;
  A.count = A.head = 0;
}

void audio_stop_local(void) {
  if (!A.active) return;
  hal_spk_stop();
  release();
  A.lv_n = 0;
}

static bool begin_stream(uint8_t stream, uint32_t rate, const char *turn) {
  audio_stop_local();
  if (g_core.board->speaker_rate == 0 || hal_spk_open(rate) != GADGET_OK) {
    hal_log(GADGET_LOG_WARN, TAG, "no speaker at %u Hz", (unsigned)rate);
    return false;
  }
  A.cap = (size_t)rate * GADGET_JITTER_BUFFER_MS / 1000u;
  A.ring = malloc(A.cap * sizeof(int16_t));
  if (A.ring == NULL) return false;
  A.active = true;
  A.stream = stream;
  A.rate = rate;
  snprintf(A.turn, sizeof A.turn, "%s", turn ? turn : "");
  A.head = A.count = 0;
  A.ended = A.playing = A.overflow_logged = false;
  A.received = A.written = 0;
  g_core.model.reply.speak_elapsed_ms = 0;
  g_core.model.reply.speak_total_ms = 0;
  return true;
}

static void push_samples(const int16_t *pcm, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (A.count == A.cap) {
      if (!A.overflow_logged) hal_log(GADGET_LOG_WARN, TAG, "speech arrived more than 1 s ahead; dropping");
      A.overflow_logged = true;
      return;
    }
    A.ring[(A.head + A.count) % A.cap] = pcm[i];
    A.count++;
    A.received++;
  }
}

bool audio_on_msg(const gp_msg_t *m) {
  switch (m->op) {
    case GP_OP_SPEAK_BEGIN: {
      const gp_speak_begin_t *b = &m->m.speak_begin;
      const char *turn = b->turn;
      if (turn != NULL && strcmp(turn, interaction_turn()) != 0) break; /* speech for an old turn */
      if (g_core.f.recording) break;                                     /* never play into the mic */
      begin_stream(b->stream, b->rate, turn);
      break;
    }
    case GP_OP_SPEAK_END:
      if (A.active && m->m.speak_end.stream == A.stream) {
        A.ended = true;
        g_core.model.reply.speak_total_ms = (uint32_t)(A.received * 1000u / A.rate);
      }
      break;
    case GP_OP_SPEAK_STOP:
      if (A.active && m->m.speak_stop.stream == A.stream) audio_stop_local();
      break;
    default:
      return false;
  }
  return true;
}

void audio_on_binary(uint8_t stream, const uint8_t *payload, size_t len) {
  if (!A.active || stream != A.stream || A.ended) return;
  static int16_t pcm[GADGET_BINARY_FRAME_MAX / 2]; /* 8 KiB: never on the task stack (core is single-threaded) */
  size_t n = len / 2;
  for (size_t i = 0; i < n; i++) pcm[i] = (int16_t)(uint16_t)(payload[2 * i] | (payload[2 * i + 1] << 8));
  push_samples(pcm, n);
}

static void feed(void) {
  uint64_t now = g_core.now;
  if (!A.playing && (A.count * 1000u / A.rate >= PREBUFFER_MS || A.ended)) A.playing = true;
  if (!A.playing) return;
  size_t block = A.rate / 50u; /* 20 ms */
  while (A.count > 0) {
    size_t run = A.cap - A.head;
    if (run > A.count) run = A.count;
    if (run > block) run = block;
    uint32_t ahead = hal_spk_buffered_ms();
    size_t n = hal_spk_write(&A.ring[A.head], run);
    if (n == 0) break;
    if (A.lv_n < LEVEL_QUEUE) {
      size_t slot = (A.lv_head + A.lv_n) % LEVEL_QUEUE;
      A.lv[slot].at = now + ahead;
      A.lv[slot].level = level_of(&A.ring[A.head], n);
      A.lv_n++;
    }
    A.head = (A.head + n) % A.cap;
    A.count -= n;
    A.written += n;
    if (n < run) break;
  }
}

void audio_tick(void) {
  uint64_t now = g_core.now;
  ui_model_t *m = &g_core.model;
  if (A.active) {
    feed();
    uint32_t buffered = hal_spk_buffered_ms();
    uint32_t written_ms = (uint32_t)(A.written * 1000u / A.rate);
    m->reply.speak_elapsed_ms = written_ms > buffered ? written_ms - buffered : 0;
    if (A.ended && A.count == 0 && buffered == 0) release(); /* played out */
  }
  uint8_t target = A.active ? A.level : 0;
  bool popped = false;
  while (A.lv_n > 0 && A.lv[A.lv_head].at <= now) {
    target = A.lv[A.lv_head].level;
    popped = true;
    A.lv_head = (A.lv_head + 1) % LEVEL_QUEUE;
    A.lv_n--;
  }
  if (!popped && A.lv_n == 0 && !A.active) target = 0;
  if (target > A.level) {
    A.level = target;
    A.level_changed = now;
  } else if (target < A.level && now - A.level_changed >= LEVEL_DROP_MS) {
    A.level--;
    A.level_changed = now;
  }
  m->speak_level = A.level;
  g_core.f.speaking = A.active && A.turn[0] != '\0' && strcmp(A.turn, interaction_turn()) == 0;
}

bool audio_active(void) { return A.active; }

void audio_init(void) { memset(&A, 0, sizeof A); }

void audio_deinit(void) {
  free(A.ring);
  memset(&A, 0, sizeof A);
}
