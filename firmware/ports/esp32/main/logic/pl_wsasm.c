/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_wsasm.h"

#include <string.h>

void pl_wsasm_init(pl_wsasm_t *a, uint8_t *buf, size_t text_max, size_t binary_max) {
  a->buf = buf;
  a->text_max = text_max;
  a->binary_max = binary_max;
  pl_wsasm_reset(a);
}

void pl_wsasm_reset(pl_wsasm_t *a) {
  a->len = 0;
  a->kind = 0;
  a->failed = false;
}

static pl_ws_out_t fail(pl_wsasm_t *a, pl_ws_out_t why) {
  a->failed = true;
  a->kind = 0;
  a->len = 0;
  return why;
}

pl_ws_out_t pl_wsasm_feed(pl_wsasm_t *a, uint8_t op, bool fin, size_t payload_len, size_t payload_offset,
                          const uint8_t *data, size_t len, const uint8_t **msg, size_t *msg_len) {
  if (a->failed) {
    return PL_WS_NONE;
  }
  if (op == PL_WS_OP_PING || op == PL_WS_OP_PONG) {
    return payload_offset == 0 ? PL_WS_CONTROL : PL_WS_NONE;
  }
  if (op == PL_WS_OP_CLOSE) {
    return PL_WS_NONE; /* the client reports closure through WEBSOCKET_EVENT_FINISH */
  }
  if (op == PL_WS_OP_TEXT || op == PL_WS_OP_BINARY) {
    if (payload_offset == 0) {
      if (a->kind != 0) {
        return fail(a, PL_WS_PROTOCOL); /* a new message before the last one finished */
      }
      a->kind = op;
      a->len = 0;
    } else if (a->kind != op) {
      return fail(a, PL_WS_PROTOCOL);
    }
  } else if (op == PL_WS_OP_CONT) {
    if (a->kind == 0) {
      return fail(a, PL_WS_PROTOCOL);
    }
  } else {
    return fail(a, PL_WS_PROTOCOL);
  }
  size_t max = a->kind == PL_WS_OP_TEXT ? a->text_max : a->binary_max;
  if (len > max - a->len) {
    return fail(a, PL_WS_TOO_BIG);
  }
  if (len > 0) {
    memcpy(a->buf + a->len, data, len);
  }
  a->len += len;
  bool frame_done = payload_offset + len >= payload_len;
  if (!(frame_done && fin)) {
    return PL_WS_NONE;
  }
  pl_ws_out_t out = a->kind == PL_WS_OP_TEXT ? PL_WS_TEXT : PL_WS_BINARY;
  *msg = a->buf;
  *msg_len = a->len;
  a->kind = 0;
  return out;
}
