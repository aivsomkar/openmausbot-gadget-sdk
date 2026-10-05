/* SPDX-License-Identifier: Apache-2.0 */
#include "drv_i2s_simplex.h"

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "gadget_types.h"
#include "pl_audio.h"

#define MIC_RATE 16000u
#define DMA_DESC_NUM 6u
#define DMA_FRAME_NUM 240u
#define IO_TIMEOUT_MS 1000u

static const char *TAG = "i2s_simplex";
static i2s_chan_handle_t s_rx;
static i2s_chan_handle_t s_tx;
static unsigned s_shift;
static int32_t s_raw[GADGET_MIC_FRAME_SAMPLES];

static esp_err_t mic_read(int16_t *pcm, size_t samples) {
  if (samples > GADGET_MIC_FRAME_SAMPLES) {
    return ESP_ERR_INVALID_SIZE;
  }
  size_t got = 0;
  esp_err_t e = i2s_channel_read(s_rx, s_raw, samples * sizeof(int32_t), &got, IO_TIMEOUT_MS);
  if (e != ESP_OK || got != samples * sizeof(int32_t)) {
    return e != ESP_OK ? e : ESP_FAIL;
  }
  pl_pcm_from_i32(s_raw, pcm, samples, s_shift);
  return ESP_OK;
}

static esp_err_t spk_write(const int16_t *pcm, size_t samples) {
  size_t written = 0;
  return i2s_channel_write(s_tx, pcm, samples * sizeof(int16_t), &written, IO_TIMEOUT_MS);
}

esp_err_t drv_i2s_simplex_init(const drv_i2s_simplex_cfg_t *c, board_audio_t *out) {
  s_shift = c->mic_shift;
  i2s_chan_config_t rx_chan = I2S_CHANNEL_DEFAULT_CONFIG(c->mic_port, I2S_ROLE_MASTER);
  ESP_RETURN_ON_ERROR(i2s_new_channel(&rx_chan, NULL, &s_rx), TAG, "mic channel");
  i2s_std_config_t rx_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = c->mic_bclk, .ws = c->mic_ws, .dout = I2S_GPIO_UNUSED, .din = c->mic_din},
  };
  rx_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; /* INMP441 L/R pin tied to GND */
  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &rx_cfg), TAG, "mic std");
  ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "mic enable");

  i2s_chan_config_t tx_chan = I2S_CHANNEL_DEFAULT_CONFIG(c->spk_port, I2S_ROLE_MASTER);
  tx_chan.dma_desc_num = DMA_DESC_NUM;
  tx_chan.dma_frame_num = DMA_FRAME_NUM;
  tx_chan.auto_clear = true;
  ESP_RETURN_ON_ERROR(i2s_new_channel(&tx_chan, &s_tx, NULL), TAG, "amp channel");
  const i2s_std_config_t tx_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(c->spk_rate),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = c->spk_bclk, .ws = c->spk_ws, .dout = c->spk_dout, .din = I2S_GPIO_UNUSED},
  };
  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &tx_cfg), TAG, "amp std");
  ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "amp enable");

  *out = (board_audio_t){
    .mic_rate = MIC_RATE,
    .spk_rate = c->spk_rate,
    .spk_latency_ms = DMA_DESC_NUM * DMA_FRAME_NUM * 1000u / c->spk_rate,
    .mic_read = mic_read,
    .spk_write = spk_write,
    .spk_set_volume = NULL, /* hal_audio.c scales in software */
    .spk_set_mute = NULL,   /* no codec: up to spk_latency_ms plays out after hal_spk_stop() */
  };
  return ESP_OK;
}
