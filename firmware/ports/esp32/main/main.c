/* SPDX-License-Identifier: Apache-2.0 */
/* Toolchain smoke app: proves that ESP-IDF, the managed components, core
 * and ui build for this board, and prints what it runs on. */
#include <inttypes.h>

#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "gadget_board.h"
#include "sdkconfig.h"

static const char *TAG = "main";

void app_main(void) {
  const gadget_board_t *board = gadget_board_by_id(CONFIG_GADGET_BOARD_ID);
  uint32_t bytes = 0;
  esp_flash_get_physical_size(esp_flash_default_chip, &bytes);
  ESP_LOGI(TAG, "%s (%s), firmware %s, flash %" PRIu32 " MB fitted", board != NULL ? board->display_name : "unknown board",
           CONFIG_GADGET_BOARD_ID, esp_app_get_description()->version, bytes / (1024u * 1024u));
}
