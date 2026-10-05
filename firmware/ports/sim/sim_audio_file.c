/* firmware/ports/sim/sim_audio_file.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* File and null audio (sim_hal.h): the mic replays a 16 kHz mono PCM16 WAV
 * from its start on each TALK hold (silence after its end, or always when
 * there is no file); the speaker drains in real (virtual) time and writes
 * what it played to a WAV. Timing follows the main loop's clock. */
#include <stdlib.h>
#include <string.h>
#include "gadget_hal.h"
#include "sim_hal.h"
#include "sim_internal.h"

#define SPK_BUFFER_MS 200u

static struct {
  int16_t *mic;            /* the whole input WAV */
  size_t mic_len, mic_pos;
  bool mic_on;
  uint64_t mic_next;       /* when the next 20 ms frame is due; 0 = at the next pump */
  FILE *out;
  const char *out_path;
  uint32_t out_rate;
  uint32_t out_samples;    /* data written to the WAV so far */
  uint32_t rate;           /* the open speaker rate */
  int16_t *queue;          /* queued but not yet played */
  size_t queued, cap;
  uint64_t last_pump;
} F;

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/* Loads a PCM16 mono 16 kHz WAV; NULL (with a message) otherwise. */
static int16_t *load_wav(const char *path, size_t *samples) {
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    fprintf(stderr, "gadget-sim: cannot open %s\n", path);
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *b = malloc((size_t)n);
  size_t got = b ? fread(b, 1, (size_t)n, f) : 0;
  fclose(f);
  int16_t *pcm = NULL;
  if (got >= 12 && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WAVE", 4) == 0) {
    bool fmt_ok = false;
    size_t off = 12;
    while (off + 8 <= got) {
      uint32_t len = rd32(b + off + 4);
      if (memcmp(b + off, "fmt ", 4) == 0 && len >= 16) {
        fmt_ok = rd16(b + off + 8) == 1 && rd16(b + off + 10) == 1 && rd32(b + off + 12) == 16000 &&
                 rd16(b + off + 22) == 16;
      } else if (memcmp(b + off, "data", 4) == 0 && fmt_ok) {
        size_t bytes = len > got - off - 8 ? got - off - 8 : len;
        *samples = bytes / 2;
        pcm = malloc(*samples * sizeof(int16_t) + 1);
        for (size_t i = 0; i < *samples; i++) pcm[i] = (int16_t)rd16(b + off + 8 + 2 * i);
        break;
      }
      off += 8 + len + (len & 1u);
    }
  }
  free(b);
  if (pcm == NULL) fprintf(stderr, "gadget-sim: %s is not a 16 kHz mono 16-bit PCM WAV\n", path);
  return pcm;
}

static void write_header(void) {
  uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0};
  wr32(h + 24, F.out_rate);
  wr32(h + 28, F.out_rate * 2u);
  h[32] = 2;
  h[34] = 16;
  memcpy(h + 36, "data", 4);
  wr32(h + 40, F.out_samples * 2u);
  wr32(h + 4, 36u + F.out_samples * 2u);
  fseek(F.out, 0, SEEK_SET);
  fwrite(h, 1, sizeof h, F.out);
  fseek(F.out, 0, SEEK_END);
}

static void open_out(uint32_t rate) {
  if (F.out != NULL || F.out_path == NULL) return;
  F.out_rate = rate;
  /* after a restart (--boot > 0) keep what the earlier boots played */
  if (g_sim.boot > 0 && (F.out = fopen(F.out_path, "r+b")) != NULL) {
    uint8_t h[44];
    if (fread(h, 1, sizeof h, F.out) == sizeof h && memcmp(h, "RIFF", 4) == 0) {
      F.out_rate = rd32(h + 24);
      F.out_samples = rd32(h + 40) / 2u;
      fseek(F.out, 0, SEEK_END);
      return;
    }
    fclose(F.out);
  }
  F.out = fopen(F.out_path, "w+b");
  if (F.out == NULL) {
    fprintf(stderr, "gadget-sim: cannot write %s\n", F.out_path);
    F.out_path = NULL;
    return;
  }
  F.out_samples = 0;
  write_header();
}

static gadget_status_t mic_start(uint32_t rate) {
  (void)rate;
  F.mic_on = true;
  F.mic_pos = 0;
  F.mic_next = 0;
  return GADGET_OK;
}

static void mic_stop(void) { F.mic_on = false; }

static gadget_status_t spk_open(uint32_t rate) {
  if (rate != 16000 && rate != 24000) return GADGET_ERR_UNSUPPORTED;
  if (rate != F.rate) F.queued = 0;
  F.rate = rate;
  F.cap = rate * SPK_BUFFER_MS / 1000u;
  int16_t *q = realloc(F.queue, F.cap * sizeof(int16_t));
  if (q == NULL) return GADGET_ERR_NO_MEM;
  F.queue = q;
  open_out(rate);
  return GADGET_OK;
}

static size_t spk_write(const int16_t *pcm, size_t samples) {
  if (F.rate == 0) return 0;
  size_t room = F.cap - F.queued;
  size_t n = samples < room ? samples : room;
  memcpy(F.queue + F.queued, pcm, n * sizeof(int16_t));
  F.queued += n;
  return n;
}

static uint32_t spk_buffered_ms(void) { return F.rate ? (uint32_t)(F.queued * 1000u / F.rate) : 0; }
static void spk_stop(void) { F.queued = 0; }
static void spk_set_volume(uint8_t pct) { (void)pct; }

static void pump(uint64_t now) {
  if (F.mic_on) {
    if (F.mic_next == 0) F.mic_next = now + 20;
    while (now >= F.mic_next) {
      int16_t frame[GADGET_MIC_FRAME_SAMPLES] = {0};
      for (size_t i = 0; i < GADGET_MIC_FRAME_SAMPLES && F.mic_pos < F.mic_len; i++) frame[i] = F.mic[F.mic_pos++];
      gadget_event_t ev = {.type = GADGET_EV_MIC_FRAME};
      ev.u.mic.pcm = frame;
      ev.u.mic.samples = GADGET_MIC_FRAME_SAMPLES;
      sim_post_event(&ev);
      F.mic_next += 20;
    }
  }
  uint64_t elapsed = F.last_pump ? now - F.last_pump : 0;
  F.last_pump = now;
  if (F.rate == 0 || F.queued == 0) return;
  size_t played = (size_t)(elapsed * F.rate / 1000u);
  if (played > F.queued) played = F.queued;
  if (played == 0) return;
  if (F.out != NULL) {
    for (size_t i = 0; i < played; i++) {
      uint8_t le[2] = {(uint8_t)((uint16_t)F.queue[i] & 0xff), (uint8_t)((uint16_t)F.queue[i] >> 8)};
      fwrite(le, 1, 2, F.out);
    }
    F.out_samples += (uint32_t)played;
    write_header();
    fflush(F.out);
  }
  memmove(F.queue, F.queue + played, (F.queued - played) * sizeof(int16_t));
  F.queued -= played;
}

static const sim_audio_backend_t BACKEND = {mic_start, mic_stop, spk_open, spk_write,
                                             spk_buffered_ms, spk_stop, spk_set_volume, pump};

const sim_audio_backend_t *sim_audio_file_backend(const char *mic_wav, const char *spk_wav) {
  memset(&F, 0, sizeof F);
  if (mic_wav != NULL) {
    F.mic = load_wav(mic_wav, &F.mic_len);
    if (F.mic == NULL) return NULL;
  }
  F.out_path = spk_wav;
  return &BACKEND;
}
