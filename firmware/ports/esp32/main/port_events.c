/* SPDX-License-Identifier: Apache-2.0 */
/* The one queue between driver tasks and the gadget task. */
#include <stdatomic.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gadget_core.h"
#include "pl_event.h"
#include "port.h"

#define PORT_QUEUE_LEN 128
#define PORT_POST_WAIT_MS 2000

typedef enum { PORT_MSG_GADGET = 1, PORT_MSG_WIFI } port_msg_kind_t;

typedef struct {
  port_msg_kind_t kind;
  union {
    gadget_event_t ev;       /* owns its payload (pl_event_copy) */
    port_wifi_msg_t wifi;
  } u;
} port_msg_t;

static const char *TAG = "port";
static QueueHandle_t s_q;
static TaskHandle_t s_main_task;
static atomic_uint s_dropped;

esp_err_t port_events_init(void) {
  s_q = xQueueCreate(PORT_QUEUE_LEN, sizeof(port_msg_t));
  return s_q != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

void port_set_main_task(void) { s_main_task = xTaskGetCurrentTaskHandle(); }

static bool post(const port_msg_t *m, bool may_wait) {
  TickType_t wait = (may_wait && xTaskGetCurrentTaskHandle() != s_main_task) ? pdMS_TO_TICKS(PORT_POST_WAIT_MS) : 0;
  if (xQueueSend(s_q, m, wait) == pdTRUE) {
    return true;
  }
  atomic_fetch_add(&s_dropped, 1u);
  return false;
}

bool port_post_event(const gadget_event_t *ev) {
  port_msg_t m = {.kind = PORT_MSG_GADGET};
  if (pl_event_copy(&m.u.ev, ev) != GADGET_OK) {
    atomic_fetch_add(&s_dropped, 1u);
    return false;
  }
  if (!post(&m, ev->type != GADGET_EV_MIC_FRAME)) {
    pl_event_free(&m.u.ev);
    return false;
  }
  return true;
}

bool port_post_wifi(const port_wifi_msg_t *w) {
  port_msg_t m = {.kind = PORT_MSG_WIFI};
  m.u.wifi = *w;
  return post(&m, true);
}

void port_drain(void) {
  port_msg_t m;
  while (xQueueReceive(s_q, &m, 0) == pdTRUE) {
    if (m.kind == PORT_MSG_GADGET) {
      if (m.u.ev.type == GADGET_EV_WS_OPEN || m.u.ev.type == GADGET_EV_WS_CLOSED) {
        port_ws_note(m.u.ev.type);
      }
      core_event(&m.u.ev);
      pl_event_free(&m.u.ev);
    } else if (m.kind == PORT_MSG_WIFI) {
      port_wifi_on_msg(&m.u.wifi);
    }
  }
  unsigned dropped = atomic_exchange(&s_dropped, 0u);
  if (dropped > 0) {
    ESP_LOGW(TAG, "dropped %u queued events (queue full or out of memory)", dropped);
  }
}
