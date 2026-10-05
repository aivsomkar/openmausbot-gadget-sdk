/* SPDX-License-Identifier: Apache-2.0 */
/* Byte accounting for the OTA write queue (gadget_hal.h: hal_ota_write
 * copies into a queue of at least 64 KiB + 4 KiB and returns BUSY when it
 * is full). hal_ota.c guards a pl_otaq_t with a spinlock. Chunks may be 1 to
 * GADGET_FW_CHUNK_MAX bytes, so hal_ota.c stages them into
 * GADGET_FW_CHUNK_MAX-byte blocks (pl_otaq_split) and sends the worker full
 * blocks only, except the image's last one: at most PL_OTAQ_MSGS_MAX
 * messages are in flight whatever the chunk sizes. */
#ifndef PL_OTAQ_H
#define PL_OTAQ_H

#include "gadget_types.h"

#define PL_OTAQ_CAP (64u * 1024u + GADGET_FW_CHUNK_MAX)
/* Full blocks that fit in PL_OTAQ_CAP, plus the image's short last block. */
#define PL_OTAQ_MSGS_MAX (PL_OTAQ_CAP / GADGET_FW_CHUNK_MAX + 1)

typedef struct {
  uint32_t size;     /* image size from hal_ota_begin() */
  uint32_t cap;      /* bytes the queue may hold */
  uint32_t queued;   /* accepted, not yet durably written */
  uint32_t next;     /* next expected offset */
} pl_otaq_t;

void pl_otaq_init(pl_otaq_t *q, uint32_t size, uint32_t cap);
/* GADGET_OK (accepted), GADGET_ERR_BUSY (queue full, try again),
 * GADGET_ERR_ARG (empty or > GADGET_FW_CHUNK_MAX, or offset != next),
 * GADGET_ERR_LIMIT (past the image size). */
gadget_status_t pl_otaq_admit(pl_otaq_t *q, uint32_t offset, size_t len);
/* Undo the last successful admit of len bytes (the chunk could not be queued). */
void pl_otaq_unadmit(pl_otaq_t *q, size_t len);
/* The worker wrote (or dropped) len bytes. */
void pl_otaq_done(pl_otaq_t *q, size_t len);

/* Where an admitted chunk of len bytes (1..GADGET_FW_CHUNK_MAX) goes when
 * the block being filled already holds `staged` bytes (< GADGET_FW_CHUNK_MAX).
 * `last` is true when the chunk ends the image. head + tail == len; a tail
 * needs a second block, and only happens when the head fills the first. */
typedef struct {
  uint32_t head;  /* bytes copied into the block being filled */
  uint32_t tail;  /* bytes copied into a new block */
  bool send_head; /* the block being filled is full, or ends the image */
  bool send_tail; /* the new block ends the image */
} pl_otaq_split_t;

pl_otaq_split_t pl_otaq_split(uint32_t staged, size_t len, bool last);

#endif /* PL_OTAQ_H */
