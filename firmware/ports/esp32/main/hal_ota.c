/* SPDX-License-Identifier: Apache-2.0 */
/* HAL OTA slot group (contract §2.5). Flash erases and writes can take
 * hundreds of milliseconds, so a worker task does every esp_ota_* call on
 * the handle; hal_ota_write() only copies the chunk into the worker's queue
 * (at most PL_OTAQ_CAP bytes in flight) and returns. Probation is core's
 * (core_config.probation_ms = 0 → 5 min); this file only reports the image
 * state and marks it valid or invalid. Anti-rollback is never enabled. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gadget_hal.h"
#include "pl_otaq.h"
#include "port.h"

#define OTA_QUEUE_LEN 64
#define OTA_FINALIZE_WAIT_MS 2000

typedef enum { OTA_MSG_BEGIN = 1, OTA_MSG_CHUNK, OTA_MSG_FINALIZE, OTA_MSG_ABORT } ota_msg_kind_t;

typedef struct {
  ota_msg_kind_t kind;
  uint32_t gen;
  uint32_t len;
  uint8_t *data;
} ota_msg_t;

static const char *TAG = "ota";
static QueueHandle_t s_q;
static SemaphoreHandle_t s_finalized;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static pl_otaq_t s_acct;          /* guarded by s_mux */
static uint32_t s_acct_gen;       /* guarded by s_mux */
static const esp_partition_t *s_part;

/* gadget task only */
static bool s_busy;
static uint32_t s_gen;

/* worker task only */
static esp_ota_handle_t s_handle;
static bool s_open;
static bool s_failed;
static uint32_t s_written;
static uint32_t s_wgen;
static volatile esp_err_t s_final_err;

static void post_error(gadget_status_t err) {
  gadget_event_t ev = {.type = GADGET_EV_OTA_ERROR};
  ev.u.ota_error.err = err;
  port_post_event(&ev);
}

static void account_done(uint32_t gen, uint32_t len) {
  taskENTER_CRITICAL(&s_mux);
  if (gen == s_acct_gen) {
    pl_otaq_done(&s_acct, len);
  }
  taskEXIT_CRITICAL(&s_mux);
}

static void close_handle(void) {
  if (s_open) {
    esp_ota_abort(s_handle);
    s_open = false;
  }
}

static void ota_task(void *arg) {
  (void)arg;
  ota_msg_t m;
  for (;;) {
    if (xQueueReceive(s_q, &m, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    switch (m.kind) {
      case OTA_MSG_BEGIN: {
        close_handle();
        s_wgen = m.gen;
        s_written = 0;
        s_failed = false;
        esp_err_t e = esp_ota_begin(s_part, OTA_WITH_SEQUENTIAL_WRITES, &s_handle);
        if (e == ESP_OK) {
          s_open = true;
        } else {
          ESP_LOGE(TAG, "begin: %s", esp_err_to_name(e));
          s_failed = true;
          post_error(GADGET_ERR_IO);
        }
        break;
      }
      case OTA_MSG_CHUNK:
        if (m.gen == s_wgen && s_open && !s_failed) {
          esp_err_t e = esp_ota_write(s_handle, m.data, m.len);
          if (e == ESP_OK) {
            s_written += m.len;
            gadget_event_t ev = {.type = GADGET_EV_OTA_WRITTEN};
            ev.u.ota_written.written = s_written;
            port_post_event(&ev);
          } else {
            ESP_LOGE(TAG, "write at %u: %s", (unsigned)s_written, esp_err_to_name(e));
            s_failed = true;
            post_error(GADGET_ERR_IO);
          }
        }
        account_done(m.gen, m.len);
        free(m.data);
        break;
      case OTA_MSG_FINALIZE:
        if (m.gen == s_wgen && s_open && !s_failed) {
          s_final_err = esp_ota_end(s_handle); /* checks the image; frees the handle either way */
          s_open = false;
        } else {
          s_final_err = ESP_ERR_INVALID_STATE;
        }
        xSemaphoreGive(s_finalized);
        break;
      case OTA_MSG_ABORT:
        close_handle();
        break;
    }
  }
}

esp_err_t port_ota_init(void) {
  s_q = xQueueCreate(OTA_QUEUE_LEN, sizeof(ota_msg_t));
  s_finalized = xSemaphoreCreateBinary();
  if (s_q == NULL || s_finalized == NULL) {
    return ESP_ERR_NO_MEM;
  }
  return xTaskCreatePinnedToCore(ota_task, "ota", 4096, NULL, 3, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static bool send_msg(const ota_msg_t *m) { return xQueueSend(s_q, m, pdMS_TO_TICKS(100)) == pdTRUE; }

gadget_status_t hal_ota_begin(uint32_t size) {
  if (s_busy) {
    return GADGET_ERR_BUSY;
  }
  const gadget_board_t *b = port_board();
  s_part = esp_ota_get_next_update_partition(NULL);
  if (s_part == NULL) {
    return GADGET_ERR_IO;
  }
  if (size == 0 || size > s_part->size || (b != NULL && size > b->ota_max)) {
    return GADGET_ERR_LIMIT;
  }
  uint32_t gen = s_gen + 1;
  taskENTER_CRITICAL(&s_mux);
  pl_otaq_init(&s_acct, size, PL_OTAQ_CAP);
  s_acct_gen = gen;
  taskEXIT_CRITICAL(&s_mux);
  ota_msg_t m = {.kind = OTA_MSG_BEGIN, .gen = gen};
  if (!send_msg(&m)) {
    return GADGET_ERR_BUSY;
  }
  s_gen = gen;
  s_busy = true;
  ESP_LOGI(TAG, "receiving %u bytes into %s", (unsigned)size, s_part->label);
  return GADGET_OK;
}

gadget_status_t hal_ota_write(uint32_t offset, const uint8_t *data, size_t len) {
  if (!s_busy) {
    return GADGET_ERR_STATE;
  }
  if (data == NULL) {
    return GADGET_ERR_ARG;
  }
  uint8_t *copy = heap_caps_malloc(len > 0 ? len : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (copy == NULL) {
    copy = malloc(len > 0 ? len : 1);
  }
  if (copy == NULL) {
    return GADGET_ERR_NO_MEM;
  }
  taskENTER_CRITICAL(&s_mux);
  gadget_status_t st = pl_otaq_admit(&s_acct, offset, len);
  taskEXIT_CRITICAL(&s_mux);
  if (st != GADGET_OK) {
    free(copy);
    return st;
  }
  memcpy(copy, data, len);
  ota_msg_t m = {.kind = OTA_MSG_CHUNK, .gen = s_gen, .len = (uint32_t)len, .data = copy};
  if (xQueueSend(s_q, &m, 0) != pdTRUE) {
    taskENTER_CRITICAL(&s_mux);
    pl_otaq_unadmit(&s_acct, len);
    taskEXIT_CRITICAL(&s_mux);
    free(copy);
    return GADGET_ERR_BUSY;
  }
  return GADGET_OK;
}

gadget_status_t hal_ota_finalize(void) {
  if (!s_busy) {
    return GADGET_ERR_STATE;
  }
  xSemaphoreTake(s_finalized, 0); /* drop a stale signal */
  ota_msg_t m = {.kind = OTA_MSG_FINALIZE, .gen = s_gen};
  if (!send_msg(&m)) {
    return GADGET_ERR_BUSY;
  }
  if (xSemaphoreTake(s_finalized, pdMS_TO_TICKS(OTA_FINALIZE_WAIT_MS)) != pdTRUE) {
    return GADGET_ERR_TIMEOUT;
  }
  if (s_final_err != ESP_OK) {
    ESP_LOGE(TAG, "image check failed: %s", esp_err_to_name(s_final_err));
    s_busy = false;
    return GADGET_ERR_IO;
  }
  return GADGET_OK;
}

gadget_status_t hal_ota_set_boot(const char *version) {
  (void)version; /* the simulator records it; the bootloader reads the image header */
  if (s_part == NULL) {
    return GADGET_ERR_STATE;
  }
  esp_err_t e = esp_ota_set_boot_partition(s_part);
  s_busy = false;
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "set boot: %s", esp_err_to_name(e));
    return GADGET_ERR_IO;
  }
  return GADGET_OK;
}

void hal_ota_abort(void) {
  if (!s_busy) {
    return;
  }
  s_gen++;
  taskENTER_CRITICAL(&s_mux);
  s_acct_gen = s_gen; /* chunks still queued no longer count */
  pl_otaq_init(&s_acct, 0, PL_OTAQ_CAP);
  taskEXIT_CRITICAL(&s_mux);
  ota_msg_t m = {.kind = OTA_MSG_ABORT, .gen = s_gen};
  if (!send_msg(&m)) {
    ESP_LOGE(TAG, "abort request dropped: queue full");
  }
  s_busy = false;
}

hal_ota_img_state_t hal_ota_running_state(void) {
  esp_ota_img_states_t st;
  esp_err_t e = esp_ota_get_state_partition(esp_ota_get_running_partition(), &st);
  if (e == ESP_ERR_NOT_SUPPORTED || e == ESP_ERR_NOT_FOUND) {
    return HAL_OTA_IMG_VALID; /* flashed over USB: no otadata entry yet */
  }
  if (e != ESP_OK) {
    return HAL_OTA_IMG_UNKNOWN;
  }
  switch (st) {
    case ESP_OTA_IMG_PENDING_VERIFY: return HAL_OTA_IMG_PENDING_VERIFY;
    case ESP_OTA_IMG_VALID:
    case ESP_OTA_IMG_UNDEFINED:
    case ESP_OTA_IMG_NEW: return HAL_OTA_IMG_VALID;
    default: return HAL_OTA_IMG_UNKNOWN;
  }
}

gadget_status_t hal_ota_mark_valid(void) {
  esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
  return e == ESP_OK ? GADGET_OK : GADGET_ERR_IO;
}

_Noreturn void hal_ota_mark_invalid_and_reboot(void) {
  esp_err_t e = esp_ota_mark_app_invalid_rollback_and_reboot(); /* returns only on failure */
  ESP_LOGE(TAG, "rollback failed (%s); restarting", esp_err_to_name(e));
  fflush(stdout);
  esp_restart();
}
