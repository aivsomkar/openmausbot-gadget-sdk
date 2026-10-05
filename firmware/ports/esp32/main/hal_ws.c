/* SPDX-License-Identifier: Apache-2.0 */
/* HAL WebSocket group on espressif/esp_websocket_client (contract §2.5).
 *
 * One worker task owns the client: it opens, sends, closes and destroys in
 * the order the gadget task asked, so no hal_ws_* call ever blocks on the
 * network. The client's own task runs on_ws_event(), which reassembles
 * messages (pl_wsasm) and queues them for core. WEBSOCKET_EVENT_FINISH is
 * dispatched exactly once when the client task ends, whatever the reason
 * (connect failure, peer close, our close, network error); it becomes the
 * one GADGET_EV_WS_CLOSED that follows every successful hal_ws_open(). */
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gadget_hal.h"
#include "pl_util.h"
#include "pl_wsasm.h"
#include "port.h"

#define WS_CMD_QUEUE_LEN 48
#define WS_SEND_TIMEOUT_MS 5000
#define WS_CLOSE_TIMEOUT_MS 2000
#define WS_CONNECT_TIMEOUT_MS 5000
#define WS_URI_MAX 300

typedef enum { WS_CMD_OPEN = 1, WS_CMD_TEXT, WS_CMD_BINARY, WS_CMD_CLOSE, WS_CMD_REAP } ws_cmd_kind_t;

typedef struct {
  ws_cmd_kind_t kind;
  uint32_t gen;      /* the hal_ws_open() this belongs to */
  uint16_t code;     /* CLOSE */
  char *uri;         /* OPEN, malloc'd */
  uint8_t *data;     /* TEXT/BINARY, malloc'd */
  size_t len;
} ws_cmd_t;

typedef struct {
  uint32_t gen;
  pl_wsasm_t asm_;
  uint8_t *buf;
  uint16_t close_code;  /* from the peer's close frame */
} ws_conn_t;

static const char *TAG = "ws";
static QueueHandle_t s_cmds;

/* worker task only */
static esp_websocket_client_handle_t s_client;
static ws_conn_t *s_conn;

/* gadget task only */
static uint32_t s_gen;
static bool s_active; /* from hal_ws_open() until core saw GADGET_EV_WS_CLOSED */
static bool s_open;   /* between GADGET_EV_WS_OPEN and GADGET_EV_WS_CLOSED */

/* Called only from the ws worker or the client task, never the gadget task,
 * so it may block until the port queue has room. */
static void post_closed(uint16_t code) {
  gadget_event_t ev = {.type = GADGET_EV_WS_CLOSED};
  ev.u.closed.code = code;
  while (!port_post_event(&ev)) {
    vTaskDelay(pdMS_TO_TICKS(100)); /* core must see it: it re-enables hal_ws_open() */
  }
}

static void free_conn(ws_conn_t *c) {
  if (c != NULL) {
    free(c->buf);
    free(c);
  }
}

/* Runs on the client's task. */
static void on_ws_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)base;
  ws_conn_t *c = arg;
  const esp_websocket_event_data_t *d = data;
  switch (id) {
    case WEBSOCKET_EVENT_CONNECTED: {
      gadget_event_t ev = {.type = GADGET_EV_WS_OPEN};
      port_post_event(&ev);
      break;
    }
    case WEBSOCKET_EVENT_DATA: {
      if (d->op_code == PL_WS_OP_CLOSE && d->payload_offset == 0 && d->data_len >= 2) {
        c->close_code = (uint16_t)(((uint8_t)d->data_ptr[0] << 8) | (uint8_t)d->data_ptr[1]);
      }
      const uint8_t *msg = NULL;
      size_t len = 0;
      pl_ws_out_t out = pl_wsasm_feed(&c->asm_, d->op_code, d->fin, (size_t)d->payload_len, (size_t)d->payload_offset,
                                      (const uint8_t *)d->data_ptr, (size_t)d->data_len, &msg, &len);
      if (out == PL_WS_TEXT || out == PL_WS_BINARY) {
        gadget_event_t ev = {.type = out == PL_WS_TEXT ? GADGET_EV_WS_TEXT : GADGET_EV_WS_BINARY};
        ev.u.ws.data = msg;
        ev.u.ws.len = len;
        port_post_event(&ev);
      } else if (out == PL_WS_CONTROL) {
        gadget_event_t ev = {.type = GADGET_EV_WS_CONTROL};
        port_post_event(&ev);
      } else if (out == PL_WS_TOO_BIG || out == PL_WS_PROTOCOL) {
        ESP_LOGW(TAG, "closing: %s", out == PL_WS_TOO_BIG ? "message too big" : "protocol error");
        ws_cmd_t cmd = {.kind = WS_CMD_CLOSE, .gen = c->gen, .code = out == PL_WS_TOO_BIG ? 1009 : 1002};
        xQueueSend(s_cmds, &cmd, 0);
      }
      break;
    }
    case WEBSOCKET_EVENT_FINISH: {
      post_closed(c->close_code);
      ws_cmd_t cmd = {.kind = WS_CMD_REAP, .gen = c->gen};
      xQueueSend(s_cmds, &cmd, pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
      break;
    }
    default:
      break;
  }
}

static void reap(void) {
  if (s_client != NULL) {
    esp_websocket_client_destroy(s_client); /* waits for the client task to stop */
    s_client = NULL;
  }
  free_conn(s_conn);
  s_conn = NULL;
}

static void do_open(const ws_cmd_t *cmd) {
  reap();
  ws_conn_t *c = calloc(1, sizeof(*c));
  uint8_t *buf = heap_caps_malloc(GADGET_TEXT_FRAME_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf == NULL) {
    buf = malloc(GADGET_TEXT_FRAME_MAX);
  }
  if (c == NULL || buf == NULL) {
    free(c);
    free(buf);
    post_closed(0);
    return;
  }
  c->gen = cmd->gen;
  c->buf = buf;
  pl_wsasm_init(&c->asm_, buf, GADGET_TEXT_FRAME_MAX, GADGET_BINARY_FRAME_MAX);

  const esp_websocket_client_config_t cfg = {
    .uri = cmd->uri,
    .subprotocol = GADGET_SUBPROTOCOL,
    .buffer_size = GADGET_TEXT_FRAME_MAX + 64,
    .disable_auto_reconnect = true,    /* core drives reconnects (spec §4.3) */
    .disable_pingpong_discon = true,   /* core's 45 s liveness timer decides */
    .network_timeout_ms = WS_CONNECT_TIMEOUT_MS,
    .task_stack = 6144,
    .task_prio = 5,
  };
  esp_websocket_client_handle_t h = esp_websocket_client_init(&cfg);
  if (h == NULL) {
    free_conn(c);
    post_closed(0);
    return;
  }
  if (esp_websocket_register_events(h, WEBSOCKET_EVENT_ANY, on_ws_event, c) != ESP_OK ||
      esp_websocket_client_start(h) != ESP_OK) {
    esp_websocket_client_destroy(h);
    free_conn(c);
    post_closed(0);
    return;
  }
  s_client = h;
  s_conn = c;
}

static bool current(uint32_t gen) { return s_client != NULL && s_conn != NULL && s_conn->gen == gen; }

static void ws_task(void *arg) {
  (void)arg;
  ws_cmd_t cmd;
  for (;;) {
    if (xQueueReceive(s_cmds, &cmd, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    switch (cmd.kind) {
      case WS_CMD_OPEN:
        do_open(&cmd);
        free(cmd.uri);
        break;
      case WS_CMD_TEXT:
      case WS_CMD_BINARY:
        if (current(cmd.gen) && esp_websocket_client_is_connected(s_client)) {
          int r = cmd.kind == WS_CMD_TEXT
                      ? esp_websocket_client_send_text(s_client, (const char *)cmd.data, (int)cmd.len,
                                                       pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS))
                      : esp_websocket_client_send_bin(s_client, (const char *)cmd.data, (int)cmd.len,
                                                      pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
          if (r < 0) {
            ESP_LOGW(TAG, "send failed; the client drops the connection"); /* FINISH follows */
          }
        }
        free(cmd.data);
        break;
      case WS_CMD_CLOSE:
        if (current(cmd.gen)) {
          esp_err_t e = ESP_FAIL;
          if (esp_websocket_client_is_connected(s_client)) {
            e = esp_websocket_client_close_with_code(s_client, cmd.code, NULL, 0, pdMS_TO_TICKS(WS_CLOSE_TIMEOUT_MS));
          }
          if (e != ESP_OK) {
            esp_websocket_client_stop(s_client); /* still connecting, or the close handshake failed */
          }
        }
        break;
      case WS_CMD_REAP:
        if (current(cmd.gen)) {
          reap();
        }
        break;
    }
  }
}

esp_err_t port_ws_init(void) {
  s_cmds = xQueueCreate(WS_CMD_QUEUE_LEN, sizeof(ws_cmd_t));
  if (s_cmds == NULL) {
    return ESP_ERR_NO_MEM;
  }
  return xTaskCreatePinnedToCore(ws_task, "ws", 4096, NULL, 5, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void port_ws_note(gadget_event_type_t type) {
  if (type == GADGET_EV_WS_OPEN) {
    s_open = true;
  } else if (type == GADGET_EV_WS_CLOSED) {
    s_open = false;
    s_active = false;
  }
}

gadget_status_t hal_ws_open(const char *host, uint16_t port) {
  if (s_active) {
    return GADGET_ERR_BUSY; /* the previous connection has not reported CLOSED yet */
  }
  char uri[WS_URI_MAX];
  gadget_status_t st = pl_ws_uri(uri, sizeof(uri), host, port);
  if (st != GADGET_OK) {
    return st;
  }
  ws_cmd_t cmd = {.kind = WS_CMD_OPEN, .gen = s_gen + 1, .uri = strdup(uri)};
  if (cmd.uri == NULL) {
    return GADGET_ERR_NO_MEM;
  }
  if (xQueueSend(s_cmds, &cmd, 0) != pdTRUE) {
    free(cmd.uri);
    return GADGET_ERR_BUSY;
  }
  s_gen = cmd.gen;
  s_active = true;
  s_open = false;
  return GADGET_OK;
}

static gadget_status_t queue_send(ws_cmd_kind_t kind, const void *data, size_t len, size_t max) {
  if (!s_open) {
    return GADGET_ERR_BUSY;
  }
  if (len > max) {
    return GADGET_ERR_LIMIT;
  }
  ws_cmd_t cmd = {.kind = kind, .gen = s_gen, .len = len, .data = malloc(len > 0 ? len : 1)};
  if (cmd.data == NULL) {
    return GADGET_ERR_NO_MEM;
  }
  if (len > 0) {
    memcpy(cmd.data, data, len);
  }
  if (xQueueSend(s_cmds, &cmd, 0) != pdTRUE) {
    free(cmd.data);
    return GADGET_ERR_BUSY;
  }
  return GADGET_OK;
}

gadget_status_t hal_ws_send_text(const char *data, size_t len) {
  return queue_send(WS_CMD_TEXT, data, len, GADGET_TEXT_FRAME_MAX);
}

gadget_status_t hal_ws_send_binary(const uint8_t *data, size_t len) {
  return queue_send(WS_CMD_BINARY, data, len, GADGET_BINARY_FRAME_MAX);
}

void hal_ws_close(uint16_t code) {
  if (!s_active) {
    return;
  }
  ws_cmd_t cmd = {.kind = WS_CMD_CLOSE, .gen = s_gen, .code = code};
  if (xQueueSend(s_cmds, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "close request dropped: command queue full");
  }
}
