/* SPDX-License-Identifier: Apache-2.0 */
/* ESP32 entry point. app_main brings the board up in the order of contract
 * §2.17 (board, NVS, PSA, Wi-Fi), then the "gadget" task brings up display,
 * touch, audio and buttons, starts core and the UI, and runs the one loop:
 * every 10 ms deliver queued events, core_tick, ui_render, ui_tick. */
#include <inttypes.h>
#include <stdio.h>

#include "board_api.h"
#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gadget_core.h"
#include "gadget_hal.h"
#include "gadget_ui.h"
#include "nvs_flash.h"
#include "port.h"
#include "psa/crypto.h"
#include "sdkconfig.h"

#define LOOP_PERIOD_MS 10
#define GADGET_TASK_STACK 16384
#define GADGET_TASK_PRIO 4
#define GADGET_TASK_CORE 1
#define BOOT_BRIGHTNESS_PCT 80

static const char *TAG = "main";

static void log_flash_size(void) {
  uint32_t bytes = 0;
  if (esp_flash_get_physical_size(esp_flash_default_chip, &bytes) == ESP_OK) {
    ESP_LOGI(TAG, "flash: %" PRIu32 " MB fitted, 16 MB used", bytes / (1024u * 1024u));
  }
}

static esp_err_t nvs_start(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS unreadable (%s): erasing it; the gadget must pair again", esp_err_to_name(err));
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  return err;
}

static _Noreturn void fail_restart(const char *what, int code) {
  ESP_LOGE(TAG, "%s failed (%d); restarting in 10 s", what, code);
  vTaskDelay(pdMS_TO_TICKS(10000));
  esp_restart();
}

static void gadget_task(void *arg) {
  (void)arg;
  const gadget_board_t *board = port_board();
  port_set_main_task();

  esp_lcd_panel_io_handle_t io = NULL;
  esp_lcd_panel_handle_t panel = NULL;
  ESP_ERROR_CHECK(board_display_init(&io, &panel));
  esp_lcd_touch_handle_t touch = NULL;
  esp_err_t err = board_touch_init(&touch);
  if (err != ESP_OK) {
    touch = NULL;
    if (err != ESP_ERR_NOT_SUPPORTED) {
      ESP_LOGE(TAG, "touch: %s; continuing without touch", esp_err_to_name(err));
    }
  }
  ESP_ERROR_CHECK(port_display_init(board, io, panel, touch != NULL));
  board_set_brightness(BOOT_BRIGHTNESS_PCT);

  board_audio_t audio = {0};
  err = board_audio_init(&audio);
  if (err == ESP_OK) {
    err = port_audio_start(&audio);
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "audio: %s; continuing without mic and speaker", esp_err_to_name(err));
  }
  ESP_ERROR_CHECK(board_buttons_init());
  port_input_init(board, touch);
  ESP_ERROR_CHECK(port_ws_init());
  ESP_ERROR_CHECK(port_mdns_init());
  ESP_ERROR_CHECK(port_ota_init());

  const core_config_t cfg = {
    .board = board,
    .fw_version = esp_app_get_description()->version,
    .prng_seed = 0,
    .fail_probation = false,
    .default_name = NULL,
    .probation_ms = 0,
  };
  gadget_status_t st = core_init(&cfg);
  if (st != GADGET_OK) {
    fail_restart("core_init", (int)st);
  }
  st = ui_init(board, esp_random());
  if (st != GADGET_OK) {
    fail_restart("ui_init", (int)st);
  }

  TickType_t last = xTaskGetTickCount();
  for (;;) {
    uint64_t now = hal_now_ms();
    port_drain();
    port_wifi_tick(now);
    port_input_poll();
    core_tick(now);
    ui_render(core_ui_model());
    ui_tick(now);
    vTaskDelayUntil(&last, pdMS_TO_TICKS(LOOP_PERIOD_MS));
  }
}

void app_main(void) {
  ESP_ERROR_CHECK(board_early_init());
  ESP_ERROR_CHECK(port_system_init());
  const gadget_board_t *board = gadget_board_by_id(CONFIG_GADGET_BOARD_ID);
  if (board == NULL) {
    ESP_LOGE(TAG, "CONFIG_GADGET_BOARD_ID \"%s\" is not in core's board table", CONFIG_GADGET_BOARD_ID);
    abort();
  }
  port_set_board(board);
  ESP_ERROR_CHECK(port_events_init());
  ESP_ERROR_CHECK(port_console_start());
  ESP_LOGI(TAG, "%s, firmware %s", board->display_name, esp_app_get_description()->version);
  log_flash_size();
  ESP_ERROR_CHECK(nvs_start());
  psa_status_t ps = psa_crypto_init();
  if (ps != PSA_SUCCESS) {
    fail_restart("psa_crypto_init", (int)ps);
  }
  ESP_ERROR_CHECK(port_wifi_start());
  if (xTaskCreatePinnedToCore(gadget_task, "gadget", GADGET_TASK_STACK, NULL, GADGET_TASK_PRIO, NULL,
                              GADGET_TASK_CORE) != pdPASS) {
    fail_restart("gadget task", 0);
  }
}
