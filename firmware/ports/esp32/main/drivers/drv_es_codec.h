/* SPDX-License-Identifier: Apache-2.0 */
/* ES8311 (speaker DAC) + ES7210 (mic ADC) on one I2S port and one I2C bus
 * (amoled-175c, amoled-175, lcd-154), on espressif/esp_codec_dev. The two
 * codecs share MCLK/BCLK/LRCK, so both directions run at 16 kHz (A13). The
 * mic is MIC1 only, on the left slot of ES7210 SDOUT1 (A18); MIC3 (echo
 * reference) is a v2 item. */
#ifndef DRV_ES_CODEC_H
#define DRV_ES_CODEC_H

#include <stdint.h>

#include "board_api.h"
#include "driver/i2c_master.h"

typedef struct {
  i2c_master_bus_handle_t bus;
  int i2c_port;
  int i2s_port;
  int mclk, bclk, ws, dout, din, pa;
  float mic_gain_db;
  uint8_t volume;             /* 0..100 at boot */
} drv_es_codec_cfg_t;

esp_err_t drv_es_codec_init(const drv_es_codec_cfg_t *cfg, board_audio_t *out);

#endif /* DRV_ES_CODEC_H */
