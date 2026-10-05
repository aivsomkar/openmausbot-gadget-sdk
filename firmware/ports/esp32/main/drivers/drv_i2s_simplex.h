/* SPDX-License-Identifier: Apache-2.0 */
/* devkit audio: an INMP441 I2S microphone and a MAX98357A I2S amplifier on
 * two separate I2S controllers, so the speaker may run at 24 kHz while the
 * mic runs at 16 kHz (spec §4.3 rate rule). */
#ifndef DRV_I2S_SIMPLEX_H
#define DRV_I2S_SIMPLEX_H

#include <stdint.h>

#include "board_api.h"

typedef struct {
  int mic_port, mic_bclk, mic_ws, mic_din;
  int spk_port, spk_bclk, spk_ws, spk_dout;
  uint32_t spk_rate;     /* 24000 */
  unsigned mic_shift;    /* 32-bit slot → PCM16: 16 is unity; 14 adds ~12 dB */
} drv_i2s_simplex_cfg_t;

esp_err_t drv_i2s_simplex_init(const drv_i2s_simplex_cfg_t *cfg, board_audio_t *out);

#endif /* DRV_I2S_SIMPLEX_H */
