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
  size_t carry;                    /* chime samples at the head, ahead of a spoken post's speech */
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

/* Playback of the current turn's speech: the reply's speech stream, whose
 * speak_elapsed_ms and speak_total_ms the model shows (contract §2.8). A
 * post's chime and speech belong to no turn and leave them alone. */
static bool reply_speech(void) { return A.turn[0] != '\0' && strcmp(A.turn, interaction_turn()) == 0; }

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
  if (A.active && A.stream == 0 && stream != 0 && turn == NULL && rate == A.rate) {
    /* A post's chime is still playing when its speech begins (spec §4.6): the
     * speech queues behind the chime's unplayed samples instead of cutting them. */
    A.stream = stream;
    A.ended = A.overflow_logged = false;
    A.carry = A.count;
    A.playing = A.carry > 0;
    A.received = A.written = 0;
    return true;
  }
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
  A.head = A.count = A.carry = 0;
  A.ended = A.playing = A.overflow_logged = false;
  A.received = A.written = 0;
  if (turn != NULL) {
    g_core.model.reply.speak_elapsed_ms = 0;
    g_core.model.reply.speak_total_ms = 0;
  }
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
      if (g_core.f.recording) break; /* the recording replaced that turn; a held press may still be a tap */
      begin_stream(b->stream, b->rate, turn);
      break;
    }
    case GP_OP_SPEAK_END:
      if (A.active && m->m.speak_end.stream == A.stream) {
        A.ended = true;
        if (reply_speech()) g_core.model.reply.speak_total_ms = (uint32_t)(A.received * 1000u / A.rate);
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
  if (g_core.f.mic_live) return; /* never play into a live mic; start_recording() drops the stream */
  uint64_t now = g_core.now;
  if (!A.playing && (A.count * 1000u / A.rate >= PREBUFFER_MS || A.ended)) A.playing = true;
  if (!A.playing) return;
  size_t block = A.rate / 50u; /* 20 ms */
  while (A.count > 0) {
    size_t run = A.cap - A.head;
    if (run > A.count) run = A.count;
    if (run > block) run = block;
    if (A.carry > 0 && run > A.carry) run = A.carry;
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
    if (A.carry > 0) {
      A.carry -= n;
      if (A.carry == 0 && !A.ended) {
        A.playing = false; /* the chime is out: the speech pre-buffers like any other */
        break;
      }
    }
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
    if (reply_speech()) m->reply.speak_elapsed_ms = written_ms > buffered ? written_ms - buffered : 0;
    if (A.ended && A.count == 0 && buffered == 0) release(); /* played out */
  }
  if (!A.active) { /* contract §2.7: 0 when no stream plays (stopped, dropped or played out) */
    A.lv_n = 0;
    A.level = 0;
  }
  uint8_t target = A.active ? A.level : 0;
  while (A.lv_n > 0 && A.lv[A.lv_head].at <= now) {
    target = A.lv[A.lv_head].level;
    A.lv_head = (A.lv_head + 1) % LEVEL_QUEUE;
    A.lv_n--;
  }
  if (target > A.level) {
    A.level = target;
    A.level_changed = now;
  } else if (target < A.level && now - A.level_changed >= LEVEL_DROP_MS) {
    A.level--;
    A.level_changed = now;
  }
  m->speak_level = A.level;
  g_core.f.speaking = A.active && reply_speech() && !g_core.f.mic_live;
}

bool audio_active(void) { return A.active; }
bool audio_speech_active(void) { return A.active && A.stream != 0; }

/* ---- chime: 300 ms, two tones, integer-only ------------------------------------- */

#define CHIME_MS 300u
#define CHIME_AMP 6000
#define CHIME_FADE_MS 5u

/* round(32767 * sin(i * pi / 32)), i = 0..16: a quarter wave */
static const int16_t QSIN[17] = {0,     3212,  6393,  9512,  12539, 15446, 18204, 20787, 23170,
                                 25329, 27245, 28898, 30273, 31356, 32137, 32609, 32767};

/* sin of a 16-bit phase (65536 = one turn), linear between table points */
static int32_t sine(uint16_t phase) {
  uint32_t quad = phase >> 14;
  uint32_t p = phase & 0x3FFFu;
  if (quad & 1u) p = 0x4000u - p;
  uint32_t i = p >> 10, frac = p & 1023u;
  int32_t v = i >= 16 ? QSIN[16] : QSIN[i] + ((QSIN[i + 1] - QSIN[i]) * (int32_t)frac) / 1024;
  return (quad & 2u) ? -v : v;
}

bool audio_play_chime(void) {
  uint32_t rate = g_core.board->speaker_rate;
  if (rate == 0 || g_core.f.mic_live) return false;
  if (A.active && A.stream != 0) return false; /* never over speech */
  if (!begin_stream(0, rate, NULL)) return false; /* stream 0: host streams are 1..255 */
  size_t n = (size_t)rate * CHIME_MS / 1000u, half = n / 2, fade = (size_t)rate * CHIME_FADE_MS / 1000u;
  uint32_t phase = 0;
  for (size_t k = 0; k < n; k++) {
    bool first = k < half;
    phase += (first ? 880u : 1320u) * 65536u / rate;
    size_t pos = first ? k : k - half, len = first ? half : n - half;
    int32_t env = CHIME_AMP;
    if (pos < fade) env = env * (int32_t)pos / (int32_t)fade;
    else if (len - pos < fade) env = env * (int32_t)(len - pos) / (int32_t)fade;
    int16_t s = (int16_t)(sine((uint16_t)phase) * env / 32767);
    push_samples(&s, 1);
  }
  A.ended = true;
  return true;
}

void audio_init(void) { memset(&A, 0, sizeof A); }

void audio_deinit(void) {
  free(A.ring);
  memset(&A, 0, sizeof A);
}
