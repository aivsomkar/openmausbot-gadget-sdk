/* SPDX-License-Identifier: Apache-2.0 */
/* Reassembles esp_websocket_client WEBSOCKET_EVENT_DATA pieces into whole
 * messages. A frame larger than the client's buffer arrives as several
 * pieces (payload_offset, payload_len), and a fragmented message arrives as
 * a TEXT or BINARY frame followed by CONTINUATION frames until FIN. */
#ifndef PL_WSASM_H
#define PL_WSASM_H

#include "gadget_types.h"

#define PL_WS_OP_CONT 0x0u
#define PL_WS_OP_TEXT 0x1u
#define PL_WS_OP_BINARY 0x2u
#define PL_WS_OP_CLOSE 0x8u
#define PL_WS_OP_PING 0x9u
#define PL_WS_OP_PONG 0xAu

typedef enum {
  PL_WS_NONE = 0,   /* nothing complete yet */
  PL_WS_TEXT,       /* msg, msg_len: one whole text message */
  PL_WS_BINARY,     /* msg, msg_len: one whole binary message */
  PL_WS_CONTROL,    /* a ping or pong arrived */
  PL_WS_TOO_BIG,    /* message over its limit: close with 1009 */
  PL_WS_PROTOCOL    /* bad opcode or fragment order: close with 1002 */
} pl_ws_out_t;

typedef struct {
  uint8_t *buf;          /* at least max(text_max, binary_max) bytes */
  size_t text_max;
  size_t binary_max;
  size_t len;            /* bytes of the message in progress */
  uint8_t kind;          /* 0, PL_WS_OP_TEXT or PL_WS_OP_BINARY */
  bool failed;           /* a TOO_BIG/PROTOCOL was reported; ignore input until reset */
} pl_wsasm_t;

void pl_wsasm_init(pl_wsasm_t *a, uint8_t *buf, size_t text_max, size_t binary_max);
void pl_wsasm_reset(pl_wsasm_t *a);
/* Feed one piece. On PL_WS_TEXT/PL_WS_BINARY, *msg points into a->buf and
 * stays valid until the next call. */
pl_ws_out_t pl_wsasm_feed(pl_wsasm_t *a, uint8_t op, bool fin, size_t payload_len, size_t payload_offset,
                          const uint8_t *data, size_t len, const uint8_t **msg, size_t *msg_len);

#endif /* PL_WSASM_H */
