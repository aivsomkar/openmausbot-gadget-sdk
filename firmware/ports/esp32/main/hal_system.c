/* SPDX-License-Identifier: Apache-2.0 */
/* HAL system group: clock, restart, log, console output. */
#include <stdarg.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "gadget_hal.h"
#include "port.h"
#include "sdkconfig.h"

static const gadget_board_t *s_board;
static SemaphoreHandle_t s_out_lock;

esp_err_t port_system_init(void) {
  s_out_lock = xSemaphoreCreateMutex();
  return s_out_lock != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

void port_set_board(const gadget_board_t *board) { s_board = board; }
const gadget_board_t *port_board(void) { return s_board; }

uint64_t hal_now_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }

_Noreturn void hal_restart(void) {
  fflush(stdout);
  esp_restart();
}

void hal_vlog(gadget_log_level_t level, const char *tag, const char *fmt, va_list ap) {
  char buf[256];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  switch (level) {
    case GADGET_LOG_ERROR: ESP_LOGE(tag, "%s", buf); break;
    case GADGET_LOG_WARN: ESP_LOGW(tag, "%s", buf); break;
    case GADGET_LOG_INFO: ESP_LOGI(tag, "%s", buf); break;
    default: ESP_LOGD(tag, "%s", buf); break;
  }
}

void hal_log(gadget_log_level_t level, const char *tag, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  hal_vlog(level, tag, fmt, ap);
  va_end(ap);
}

void hal_log_set_enabled(bool enabled) {
  esp_log_level_set("*", enabled ? (esp_log_level_t)CONFIG_LOG_DEFAULT_LEVEL : ESP_LOG_NONE);
}

/* @omb lines bypass the log system, so `log off` never hides them. The
 * mutex keeps two @omb lines from interleaving (the gadget task and the
 * console task both write). Log lines from other tasks do not take it, so
 * the line and its newline go out in one stdio call: a log line can land
 * before or after an @omb line, never between the line and its newline
 * (`@omb boot` is printed while logs are still on). */
void hal_console_write(const char *line) {
  if (s_out_lock != NULL) {
    xSemaphoreTake(s_out_lock, portMAX_DELAY);
  }
  fprintf(stdout, "%s\n", line);
  fflush(stdout);
  if (s_out_lock != NULL) {
    xSemaphoreGive(s_out_lock);
  }
}
