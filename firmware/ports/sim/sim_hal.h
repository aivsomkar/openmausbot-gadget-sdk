/* firmware/ports/sim/sim_hal.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Simulator seams between P2a's main loop and HAL backends and P2b's SDL
 * code. P2a implements sim_post_event (sim_events.c), sim_audio_use and
 * the hal_mic_* / hal_spk_* symbols (sim_audio.c), and
 * sim_audio_file_backend (sim_audio_file.c). P2b implements
 * sim_audio_sdl_backend (sim_audio_sdl.c). */
#ifndef SIM_HAL_H
#define SIM_HAL_H

#include "gadget_events.h"

/* Queue one event for core. Deep-copies every pointer payload (mic.pcm,
 * ws.data, console.line, scan.aps, mdns.hosts) and frees the copy after
 * delivery. Safe to call from any thread (SDL may run an event filter off
 * the main thread). The main loop delivers queued events in order, with
 * core_event(), before the next core_tick(). */
void sim_post_event(const gadget_event_t *ev);

/* The audio behind hal_mic_* / hal_spk_*. Each member has the semantics of
 * the HAL function of the same name (gadget_hal.h). Mic frames are posted
 * with sim_post_event(GADGET_EV_MIC_FRAME) from pump(). */
typedef struct {
  gadget_status_t (*mic_start)(uint32_t rate);
  void (*mic_stop)(void);
  gadget_status_t (*spk_open)(uint32_t rate);
  size_t (*spk_write)(const int16_t *pcm, size_t samples);
  uint32_t (*spk_buffered_ms)(void);
  void (*spk_stop)(void);
  void (*spk_set_volume)(uint8_t pct);
  void (*pump)(uint64_t now_ms);   /* main.c calls it once per loop iteration */
} sim_audio_backend_t;

/* Select the backend; sim_audio.c forwards every hal_mic_* / hal_spk_* call
 * to it. Before the first call, or with NULL, audio is null. */
void sim_audio_use(const sim_audio_backend_t *backend);
/* P2a: WAV in (--mic-file, replayed from its start on each TALK hold) and
 * WAV out (--speaker-file); a NULL path means null audio for that direction. */
const sim_audio_backend_t *sim_audio_file_backend(const char *mic_wav, const char *spk_wav);
#if defined(GADGET_WITH_SDL)
/* P2b: SDL queued audio for window mode (mic opened lazily on first TALK). */
const sim_audio_backend_t *sim_audio_sdl_backend(void);
#endif

#endif /* SIM_HAL_H */
