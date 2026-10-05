/* SPDX-License-Identifier: Apache-2.0 */
/* Byte accounting for the OTA write queue (gadget_hal.h: hal_ota_write
 * copies into a queue of at least 64 KiB + 4 KiB and returns BUSY when it
 * is full). hal_ota.c guards a pl_otaq_t with a spinlock. */
#ifndef PL_OTAQ_H
#define PL_OTAQ_H

#include "gadget_types.h"

#define PL_OTAQ_CAP (64u * 1024u + GADGET_FW_CHUNK_MAX)

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

#endif /* PL_OTAQ_H */
