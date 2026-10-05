/* SPDX-License-Identifier: Apache-2.0 */
/* Window-mode audio (contract 2.16): the Mac's (or PC's) microphone and
 * speakers through SDL2's queued audio. The capture device opens lazily on
 * the first TALK, so macOS asks for microphone access only then; after
 * about 1 s of all-zero samples while talking we print a permission hint.
 * Without an audio device (no audio driver, or a device that will not open)
 * the backend is silent, never fatal: each problem is printed once. */
#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "sim_hal.h"

#define SIM_SPK_QUEUE_MS 2000u   /* hal_spk_write accepts up to 2 s ahead */
#define SIM_SILENT_HINT_MS 1000u

static struct {
  bool tried, ok;
  bool cap_reported, play_reported; /* an open failure was printed */
  SDL_AudioDeviceID cap;
  SDL_AudioDeviceID play;
  uint32_t play_rate;
  uint8_t volume;
  bool capturing;
  uint32_t silent_ms;
  bool hinted;
  int16_t frame[GADGET_MIC_FRAME_SAMPLES]; /* the mic frame being filled */
  size_t frame_bytes;
} a = {.volume = 100};

static bool audio_ready(void) {
  if (!a.tried) {
    a.tried = true;
    a.ok = SDL_InitSubSystem(SDL_INIT_AUDIO) == 0;
    if (!a.ok) fprintf(stderr, "gadget-sim: no audio (%s); the mic and speaker are silent\n", SDL_GetError());
  }
  return a.ok;
}

static gadget_status_t mic_start(uint32_t rate) {
  if (rate != GADGET_MIC_RATE) return GADGET_ERR_UNSUPPORTED;
  if (!audio_ready()) return GADGET_OK;
  if (!a.cap) {
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = (int)GADGET_MIC_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = GADGET_MIC_FRAME_SAMPLES;
    a.cap = SDL_OpenAudioDevice(NULL, 1, &want, NULL, 0); /* SDL converts to exactly this format */
    if (!a.cap) {
      if (!a.cap_reported) fprintf(stderr, "gadget-sim: no microphone (%s); recordings are silent\n", SDL_GetError());
      a.cap_reported = true;
      return GADGET_OK; /* silent: pump() posts nothing while a.cap is 0 */
    }
  }
  SDL_ClearQueuedAudio(a.cap);
  SDL_PauseAudioDevice(a.cap, 0);
  a.capturing = true;
  a.silent_ms = 0;
  a.frame_bytes = 0;
  return GADGET_OK;
}

static void mic_stop(void) {
  if (a.cap) {
    SDL_PauseAudioDevice(a.cap, 1);
    SDL_ClearQueuedAudio(a.cap);
  }
  a.capturing = false;
  a.frame_bytes = 0;
}

static gadget_status_t spk_open(uint32_t rate) {
  if (rate != 16000u && rate != 24000u) return GADGET_ERR_UNSUPPORTED;
  if (!audio_ready()) return GADGET_OK;
  if (a.play && a.play_rate == rate) return GADGET_OK;
  if (a.play) SDL_CloseAudioDevice(a.play);
  SDL_AudioSpec want;
  SDL_zero(want);
  want.freq = (int)rate;
  want.format = AUDIO_S16SYS;
  want.channels = 1;
  want.samples = (Uint16)(rate / 50u);
  a.play = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
  if (!a.play) {
    if (!a.play_reported) fprintf(stderr, "gadget-sim: no speaker (%s); playback is silent\n", SDL_GetError());
    a.play_reported = true;
    a.play_rate = 0;
    return GADGET_OK; /* silent: spk_write() accepts and drops while a.play is 0 */
  }
  a.play_rate = rate;
  SDL_PauseAudioDevice(a.play, 0);
  return GADGET_OK;
}

static size_t spk_write(const int16_t *pcm, size_t samples) {
  if (!a.play) return samples; /* silent: accept and drop */
  uint32_t queued = SDL_GetQueuedAudioSize(a.play) / 2u;
  uint32_t cap = a.play_rate * SIM_SPK_QUEUE_MS / 1000u;
  if (queued >= cap) return 0;
  size_t n = samples < (size_t)(cap - queued) ? samples : (size_t)(cap - queued);
  int16_t chunk[480];
  for (size_t done = 0; done < n;) {
    size_t k = n - done < 480 ? n - done : 480;
    for (size_t i = 0; i < k; i++) chunk[i] = (int16_t)((int32_t)pcm[done + i] * a.volume / 100);
    SDL_QueueAudio(a.play, chunk, (Uint32)(k * 2u));
    done += k;
  }
  return n;
}

static uint32_t spk_buffered_ms(void) {
  if (!a.play || !a.play_rate) return 0;
  return (uint32_t)((uint64_t)(SDL_GetQueuedAudioSize(a.play) / 2u) * 1000u / a.play_rate);
}

static void spk_stop(void) {
  if (a.play) SDL_ClearQueuedAudio(a.play);
}

static void spk_set_volume(uint8_t pct) { a.volume = pct > 100 ? 100 : pct; }

static void pump(uint64_t now_ms) {
  (void)now_ms;
  if (!a.cap || !a.capturing) return;
  for (;;) {
    /* SDL can hand over less than asked for: fill one 20 ms frame across calls. */
    Uint32 got = SDL_DequeueAudio(a.cap, (Uint8 *)a.frame + a.frame_bytes, (Uint32)(sizeof a.frame - a.frame_bytes));
    if (got == 0) return;
    a.frame_bytes += got;
    if (a.frame_bytes < sizeof a.frame) continue;
    a.frame_bytes = 0;
    bool silent = true;
    for (size_t i = 0; i < GADGET_MIC_FRAME_SAMPLES && silent; i++) silent = a.frame[i] == 0;
    a.silent_ms = silent ? a.silent_ms + 20u : 0u;
    if (a.silent_ms >= SIM_SILENT_HINT_MS && !a.hinted) {
      a.hinted = true;
      fprintf(stderr,
              "gadget-sim: the microphone is silent. On macOS, allow your terminal app in System Settings > "
              "Privacy & Security > Microphone (to ask again: tccutil reset Microphone <your terminal's bundle id>, "
              "e.g. com.apple.Terminal), then restart it.\n");
    }
    gadget_event_t ev;
    memset(&ev, 0, sizeof ev);
    ev.type = GADGET_EV_MIC_FRAME;
    ev.u.mic.pcm = a.frame;
    ev.u.mic.samples = (uint16_t)GADGET_MIC_FRAME_SAMPLES;
    sim_post_event(&ev); /* copies the samples */
  }
}

static const sim_audio_backend_t k_backend = {
  mic_start, mic_stop, spk_open, spk_write, spk_buffered_ms, spk_stop, spk_set_volume, pump,
};

const sim_audio_backend_t *sim_audio_sdl_backend(void) { return &k_backend; }
