/* SPDX-License-Identifier: Apache-2.0 */
/* HAL OTA slot group (contract §2.5). Flash erases and writes can take
 * hundreds of milliseconds, so a worker task does every esp_ota_* call on
 * the handle; hal_ota_write() only copies the chunk into a staging block and
 * returns (at most PL_OTAQ_CAP bytes in flight). Chunks may be 1 to
 * GADGET_FW_CHUNK_MAX bytes, so the worker gets GADGET_FW_CHUNK_MAX-byte
 * blocks only, except the image's last one (pl_otaq_split): at most
 * PL_OTAQ_MSGS_MAX messages are in flight however small the chunks are. The
 * staged bytes are durably written once their block fills or the image ends,
 * so GADGET_EV_OTA_WRITTEN lags by under GADGET_FW_CHUNK_MAX bytes and
 * reaches the size with the last chunk. Probation is core's
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

/* one session's blocks, plus an ABORT and a BEGIN behind them */
_Static_assert(OTA_QUEUE_LEN >= PL_OTAQ_MSGS_MAX + 2, "OTA queue too short for PL_OTAQ_CAP");

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
static uint8_t *s_stage;     /* GADGET_FW_CHUNK_MAX-byte block being filled */
static uint32_t s_stage_len; /* bytes in it, always < GADGET_FW_CHUNK_MAX between calls */

/* worker task only */
static esp_ota_handle_t s_handle;
static bool s_open;
static bool s_failed;
static uint32_t s_written;
static uint32_t s_wgen;
static volatile esp_err_t s_final_err;
/* esp_flash_write programs the main flash straight from internal RAM only; a
 * PSRAM source goes 32 bytes per flash operation, so each block is copied
 * here first (the queued blocks stay in PSRAM). */
static uint8_t s_bounce[GADGET_FW_CHUNK_MAX];

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

/* A message belongs to the live session unless hal_ota_abort() or a later
 * hal_ota_begin() has moved s_acct_gen on since it was queued. */
static bool gen_live(uint32_t gen) {
  taskENTER_CRITICAL(&s_mux);
  bool live = gen == s_acct_gen;
  taskEXIT_CRITICAL(&s_mux);
  return live;
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
        /* an aborted session's blocks are dropped without a flash write or an event */
        if (gen_live(m.gen) && m.gen == s_wgen && s_open && !s_failed) {
          memcpy(s_bounce, m.data, m.len);
          esp_err_t e = esp_ota_write(s_handle, s_bounce, m.len);
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
        if (gen_live(m.gen) && m.gen == s_wgen && s_open && !s_failed) {
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

static uint8_t *block_alloc(void) {
  uint8_t *b = heap_caps_malloc(GADGET_FW_CHUNK_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return b != NULL ? b : malloc(GADGET_FW_CHUNK_MAX);
}

static void stage_drop(void) {
  free(s_stage);
  s_stage = NULL;
  s_stage_len = 0;
}

/* Hand the staging block to the worker. Only the gadget task sends to s_q
 * and hal_ota_write() saw two free slots before admitting, so this cannot
 * fail; if it ever did, the block is dropped and the update fails as a
 * flash error instead of stalling. */
static void stage_send(void) {
  ota_msg_t m = {.kind = OTA_MSG_CHUNK, .gen = s_gen, .len = s_stage_len, .data = s_stage};
  if (xQueueSend(s_q, &m, 0) != pdTRUE) {
    ESP_LOGE(TAG, "write queue full after the space check");
    free(s_stage);
    post_error(GADGET_ERR_IO);
  }
  s_stage = NULL;
  s_stage_len = 0;
}

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
  stage_drop();
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
  /* Everything that can fail happens before the admit: one chunk sends at
   * most two blocks, and only this task sends to s_q. */
  if (uxQueueSpacesAvailable(s_q) < 2) {
    return GADGET_ERR_BUSY;
  }
  if (s_stage == NULL && (s_stage = block_alloc()) == NULL) {
    return GADGET_ERR_NO_MEM;
  }
  uint8_t *next = NULL;
  if ((uint64_t)s_stage_len + len > GADGET_FW_CHUNK_MAX && (next = block_alloc()) == NULL) {
    return GADGET_ERR_NO_MEM;
  }
  taskENTER_CRITICAL(&s_mux);
  gadget_status_t st = pl_otaq_admit(&s_acct, offset, len);
  bool last = (uint64_t)offset + len == s_acct.size;
  taskEXIT_CRITICAL(&s_mux);
  if (st != GADGET_OK) {
    free(next);
    return st;
  }
  pl_otaq_split_t sp = pl_otaq_split(s_stage_len, len, last);
  memcpy(s_stage + s_stage_len, data, sp.head);
  s_stage_len += sp.head;
  if (sp.send_head) {
    stage_send();
    s_stage = next; /* NULL unless the chunk spills over */
    next = NULL;
  }
  if (sp.tail > 0) {
    memcpy(s_stage, data + sp.head, sp.tail);
    s_stage_len = sp.tail;
    if (sp.send_tail) {
      stage_send();
    }
  }
  free(next); /* NULL by now: a spill always fills and sends the head block */
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
  stage_drop();
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
