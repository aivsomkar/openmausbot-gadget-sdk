/* SPDX-License-Identifier: Apache-2.0 */
#include "drv_es_codec.h"

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

#define CODEC_RATE 16000u
#define DMA_DESC_NUM 6u
#define DMA_FRAME_NUM 240u

static const char *TAG = "es_codec";
static esp_codec_dev_handle_t s_spk;
static esp_codec_dev_handle_t s_mic;

static esp_err_t mic_read(int16_t *pcm, size_t samples) {
  int r = esp_codec_dev_read(s_mic, pcm, (int)(samples * sizeof(int16_t)));
  return r == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t spk_write(const int16_t *pcm, size_t samples) {
  int r = esp_codec_dev_write(s_spk, (void *)pcm, (int)(samples * sizeof(int16_t)));
  return r == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

static void spk_volume(uint8_t pct) { esp_codec_dev_set_out_vol(s_spk, pct); }

static void spk_mute(bool mute) { esp_codec_dev_set_out_mute(s_spk, mute); }

esp_err_t drv_es_codec_init(const drv_es_codec_cfg_t *c, board_audio_t *out) {
  i2s_chan_handle_t tx = NULL, rx = NULL;
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(c->i2s_port, I2S_ROLE_MASTER);
  chan.dma_desc_num = DMA_DESC_NUM;
  chan.dma_frame_num = DMA_FRAME_NUM;
  chan.auto_clear = true; /* silence on underrun */
  ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, &tx, &rx), TAG, "i2s channels");
  const i2s_std_config_t std = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CODEC_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {.mclk = c->mclk, .bclk = c->bclk, .ws = c->ws, .dout = c->dout, .din = c->din},
  };
  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &std), TAG, "tx std");
  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &std), TAG, "rx std");
  ESP_RETURN_ON_ERROR(i2s_channel_enable(tx), TAG, "tx enable");
  ESP_RETURN_ON_ERROR(i2s_channel_enable(rx), TAG, "rx enable");

  audio_codec_i2s_cfg_t i2s_cfg = {.port = (uint8_t)c->i2s_port, .rx_handle = rx, .tx_handle = tx};
  const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
  audio_codec_i2c_cfg_t spk_i2c = {.port = (uint8_t)c->i2c_port, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = c->bus};
  audio_codec_i2c_cfg_t mic_i2c = {.port = (uint8_t)c->i2c_port, .addr = ES7210_CODEC_DEFAULT_ADDR, .bus_handle = c->bus};
  const audio_codec_ctrl_if_t *spk_ctrl = audio_codec_new_i2c_ctrl(&spk_i2c);
  const audio_codec_ctrl_if_t *mic_ctrl = audio_codec_new_i2c_ctrl(&mic_i2c);
  const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
  ESP_RETURN_ON_FALSE(data_if && spk_ctrl && mic_ctrl && gpio_if, ESP_FAIL, TAG, "codec interfaces");

  es8311_codec_cfg_t es8311 = {
    .ctrl_if = spk_ctrl,
    .gpio_if = gpio_if,
    .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
    .pa_pin = (int16_t)c->pa,
    .pa_reverted = false,
    .master_mode = false,
    .use_mclk = true,
    .hw_gain = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f},
  };
  es7210_codec_cfg_t es7210 = {.ctrl_if = mic_ctrl, .master_mode = false, .mic_selected = ES7210_SEL_MIC1};
  const audio_codec_if_t *spk_if = es8311_codec_new(&es8311);
  const audio_codec_if_t *mic_if = es7210_codec_new(&es7210);
  ESP_RETURN_ON_FALSE(spk_if && mic_if, ESP_FAIL, TAG, "codecs (check the I2C addresses)");

  esp_codec_dev_cfg_t spk_dev = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = spk_if, .data_if = data_if};
  esp_codec_dev_cfg_t mic_dev = {.dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = mic_if, .data_if = data_if};
  s_spk = esp_codec_dev_new(&spk_dev);
  s_mic = esp_codec_dev_new(&mic_dev);
  ESP_RETURN_ON_FALSE(s_spk && s_mic, ESP_FAIL, TAG, "codec devices");

  esp_codec_dev_sample_info_t out_fs = {.bits_per_sample = 16, .channel = 1, .channel_mask = 0, .sample_rate = CODEC_RATE};
  /* esp_codec_dev maps a 1-channel format to slot 0 = ES7210 SDOUT1 left =
   * MIC1, the only microphone powered (A18). It ignores channel_mask here. */
  esp_codec_dev_sample_info_t in_fs = {.bits_per_sample = 16, .channel = 1, .channel_mask = 0, .sample_rate = CODEC_RATE};
  ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_spk, &out_fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "open speaker");
  ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_mic, &in_fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "open mic");
  esp_codec_dev_set_in_gain(s_mic, c->mic_gain_db);
  esp_codec_dev_set_out_vol(s_spk, c->volume);

  *out = (board_audio_t){
    .mic_rate = CODEC_RATE,
    .spk_rate = CODEC_RATE,
    .spk_latency_ms = DMA_DESC_NUM * DMA_FRAME_NUM * 1000u / CODEC_RATE,
    .mic_read = mic_read,
    .spk_write = spk_write,
    .spk_set_volume = spk_volume,
    .spk_set_mute = spk_mute,
  };
  return ESP_OK;
}
