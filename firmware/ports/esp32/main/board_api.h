/* SPDX-License-Identifier: Apache-2.0 */
/* What each boards/<id>/board.c implements (contract §2.17). main.c calls
 * these once at boot in the order of contract §2.17; hal_input.c and
 * hal_battery.c call the polling ones from the main thread. */
#ifndef BOARD_API_H
#define BOARD_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_types.h"
#include "gadget_types.h"

/* The board's audio path, filled by board_audio_init(). hal_audio.c runs
 * one mic task and one speaker task on top of it. */
typedef struct board_audio {
  uint32_t mic_rate;          /* 16000 */
  uint32_t spk_rate;          /* 16000 on codec boards (shared I2S clock), 24000 on devkit */
  uint32_t spk_latency_ms;    /* audio the TX DMA ring holds once it is full */
  /* Read exactly `samples` mono PCM16 samples. Blocks; mic task only. */
  esp_err_t (*mic_read)(int16_t *pcm, size_t samples);
  /* Write mono PCM16. Blocks until the DMA ring accepted it; speaker task only. */
  esp_err_t (*spk_write)(const int16_t *pcm, size_t samples);
  /* Codec volume 0..100, or NULL: hal_audio.c scales samples in software. */
  void (*spk_set_volume)(uint8_t pct);
  /* Codec output mute, or NULL. hal_spk_stop() mutes so the audio still in
   * the TX DMA ring is not heard; the speaker task unmutes once that ring
   * holds only silence. */
  void (*spk_set_mute)(bool mute);
} board_audio_t;

/* First call in app_main: power latches and the I2C bus. */
esp_err_t board_early_init(void);
/* Panel IO + panel, reset, initialised and switched on. */
esp_err_t board_display_init(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel);
/* ESP_ERR_NOT_SUPPORTED on boards without touch. */
esp_err_t board_touch_init(esp_lcd_touch_handle_t *tp);
esp_err_t board_audio_init(board_audio_t *out);
esp_err_t board_buttons_init(void);
/* Raw levels, true = pressed; debounced by hal_input.c. */
bool board_talk_pressed(void);
bool board_cancel_pressed(void);
/* false when the board has no battery or none is fitted. */
bool board_battery_read(gadget_battery_t *out);
void board_set_brightness(uint8_t pct);

#endif /* BOARD_API_H */
