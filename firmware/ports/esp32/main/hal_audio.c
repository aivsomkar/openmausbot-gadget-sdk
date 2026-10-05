/* SPDX-License-Identifier: Apache-2.0 */
/* HAL mic and speaker groups. The board's codec (or I2S parts) runs all the
 * time: the mic task reads 20 ms frames and forwards them only while core
 * is recording, so every frame is fresh; the speaker task drains a ring of
 * up to 1.5 s and writes silence when it is empty, so the shared I2S clock
 * of the codec boards never stops. hal_spk_stop() mutes the codec at once;
 * the speaker task drains the stale DMA ring with silence, then unmutes.
 * No echo cancellation in v1 (A18). */
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gadget_hal.h"
#include "pl_audio.h"
#include "port.h"

#define SPK_RING_MS 1500u
#define SPK_CHUNK 320u
#define AUDIO_CORE 1

static const char *TAG = "audio";
static board_audio_t s_audio;
static bool s_have_audio;
static SemaphoreHandle_t s_lock;
static pl_ring_t s_ring;
static volatile bool s_mic_on;
static volatile bool s_spk_open;
static volatile uint8_t s_volume = 80;
static volatile uint32_t s_play_end_ms; /* when the last written audio leaves the DMA ring */
static volatile bool s_unmute_pending;  /* hal_spk_stop() muted the codec */
static volatile uint32_t s_stop_gen;    /* bumped by hal_spk_stop(), under s_lock */
static uint32_t s_flush_samples;        /* one TX DMA ring plus one chunk */

static uint32_t now32(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* After hal_spk_stop(): write silence until everything that was already in
 * the TX DMA ring has played (muted), then unmute. A stop that arrives while
 * this runs leaves s_unmute_pending set, so the next loop drains again. */
static void spk_drain_then_unmute(int16_t *chunk) {
  s_unmute_pending = false;
  memset(chunk, 0, SPK_CHUNK * sizeof(int16_t));
  for (uint32_t left = s_flush_samples; left > 0;) {
    size_t k = left < SPK_CHUNK ? left : SPK_CHUNK;
    s_audio.spk_write(chunk, k);
    left -= (uint32_t)k;
  }
  if (!s_unmute_pending) {
    s_audio.spk_set_mute(false);
  }
}

static void mic_task(void *arg) {
  (void)arg;
  static int16_t frame[GADGET_MIC_FRAME_SAMPLES];
  for (;;) {
    if (s_audio.mic_read(frame, GADGET_MIC_FRAME_SAMPLES) != ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (!s_mic_on) {
      continue; /* keep the RX DMA drained */
    }
    gadget_event_t ev = {.type = GADGET_EV_MIC_FRAME};
    ev.u.mic.pcm = frame;
    ev.u.mic.samples = GADGET_MIC_FRAME_SAMPLES;
    port_post_event(&ev);
  }
}

static void spk_task(void *arg) {
  (void)arg;
  static int16_t chunk[SPK_CHUNK];
  for (;;) {
    if (s_unmute_pending) {
      spk_drain_then_unmute(chunk);
      continue;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t gen = s_stop_gen;
    size_t n = pl_ring_read(&s_ring, chunk, SPK_CHUNK);
    xSemaphoreGive(s_lock);
    if (n == 0) {
      memset(chunk, 0, sizeof(chunk));
      s_audio.spk_write(chunk, SPK_CHUNK);
      continue;
    }
    if (s_audio.spk_set_volume == NULL) {
      pl_pcm_volume(chunk, n, s_volume);
    }
    s_audio.spk_write(chunk, n);
    /* A stop that arrived while spk_write blocked has already set the end
     * to now. What this chunk added to the DMA ring is the stopped stream's
     * tail (muted on the codec boards), so it must not push the end out
     * again: hal_spk_buffered_ms() reports 0 right after a stop. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (gen == s_stop_gen) {
      s_play_end_ms = now32() + s_audio.spk_latency_ms;
    }
    xSemaphoreGive(s_lock);
  }
}

esp_err_t port_audio_start(const board_audio_t *audio) {
  s_audio = *audio;
  size_t cap = (size_t)s_audio.spk_rate * SPK_RING_MS / 1000u;
  int16_t *store = heap_caps_malloc(cap * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (store == NULL) {
    store = malloc(cap * sizeof(int16_t));
  }
  s_lock = xSemaphoreCreateMutex();
  if (store == NULL || s_lock == NULL) {
    return ESP_ERR_NO_MEM;
  }
  pl_ring_init(&s_ring, store, cap);
  s_flush_samples = s_audio.spk_rate * s_audio.spk_latency_ms / 1000u + SPK_CHUNK;
  if (s_audio.spk_set_volume != NULL) {
    s_audio.spk_set_volume(s_volume);
  }
  if (xTaskCreatePinnedToCore(mic_task, "mic", 4096, NULL, 7, NULL, AUDIO_CORE) != pdPASS ||
      xTaskCreatePinnedToCore(spk_task, "spk", 4096, NULL, 7, NULL, AUDIO_CORE) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  s_have_audio = true;
  ESP_LOGI(TAG, "mic %u Hz, speaker %u Hz", (unsigned)s_audio.mic_rate, (unsigned)s_audio.spk_rate);
  return ESP_OK;
}

gadget_status_t hal_mic_start(uint32_t rate) {
  if (!s_have_audio || rate != GADGET_MIC_RATE || rate != s_audio.mic_rate) {
    return GADGET_ERR_UNSUPPORTED;
  }
  s_mic_on = true;
  return GADGET_OK;
}

void hal_mic_stop(void) { s_mic_on = false; }

gadget_status_t hal_spk_open(uint32_t rate) {
  if (!s_have_audio || rate != s_audio.spk_rate) {
    return GADGET_ERR_UNSUPPORTED;
  }
  s_spk_open = true;
  return GADGET_OK;
}

size_t hal_spk_write(const int16_t *pcm, size_t samples) {
  if (!s_spk_open || pcm == NULL) {
    return 0;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  size_t n = pl_ring_write(&s_ring, pcm, samples);
  xSemaphoreGive(s_lock);
  return n;
}

uint32_t hal_spk_buffered_ms(void) {
  if (!s_have_audio) {
    return 0;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  size_t queued = pl_ring_count(&s_ring);
  xSemaphoreGive(s_lock);
  uint32_t ms = pl_samples_to_ms(queued, s_audio.spk_rate);
  int32_t dma = (int32_t)(s_play_end_ms - now32());
  return ms + (dma > 0 ? (uint32_t)dma : 0u);
}

void hal_spk_stop(void) {
  if (!s_have_audio) {
    return;
  }
  if (s_audio.spk_set_mute != NULL) {
    s_audio.spk_set_mute(true); /* silent now, although the DMA ring still holds audio */
    s_unmute_pending = true;    /* the speaker task drains that ring, then unmutes */
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_stop_gen++; /* a chunk the speaker task is writing now no longer counts */
  pl_ring_clear(&s_ring);
  /* Without a codec mute (devkit) up to spk_latency_ms still plays out. */
  s_play_end_ms = now32();
  xSemaphoreGive(s_lock);
}

void hal_spk_set_volume(uint8_t pct) {
  s_volume = pct > 100 ? 100 : pct;
  if (s_have_audio && s_audio.spk_set_volume != NULL) {
    s_audio.spk_set_volume(s_volume);
  }
}
