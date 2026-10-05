/* firmware/ports/sim/sim_audio.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The only definitions of hal_mic_* and hal_spk_*: they forward to the
 * backend main.c selected (sim_hal.h). Without one, audio is null. */
#include "gadget_hal.h"
#include "sim_hal.h"
#include "sim_internal.h"

static const sim_audio_backend_t *s_backend;

void sim_audio_use(const sim_audio_backend_t *backend) { s_backend = backend; }

void sim_audio_pump(uint64_t now_ms) {
  if (s_backend) s_backend->pump(now_ms);
}

gadget_status_t hal_mic_start(uint32_t rate) {
  if (rate != GADGET_MIC_RATE) return GADGET_ERR_UNSUPPORTED;
  return s_backend ? s_backend->mic_start(rate) : GADGET_OK;
}

void hal_mic_stop(void) {
  if (s_backend) s_backend->mic_stop();
}

gadget_status_t hal_spk_open(uint32_t rate) { return s_backend ? s_backend->spk_open(rate) : GADGET_OK; }

size_t hal_spk_write(const int16_t *pcm, size_t samples) {
  return s_backend ? s_backend->spk_write(pcm, samples) : samples;
}

uint32_t hal_spk_buffered_ms(void) { return s_backend ? s_backend->spk_buffered_ms() : 0; }

void hal_spk_stop(void) {
  if (s_backend) s_backend->spk_stop();
}

void hal_spk_set_volume(uint8_t pct) {
  if (s_backend) s_backend->spk_set_volume(pct);
}
