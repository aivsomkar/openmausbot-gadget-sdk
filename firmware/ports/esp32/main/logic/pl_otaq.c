/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_otaq.h"

void pl_otaq_init(pl_otaq_t *q, uint32_t size, uint32_t cap) {
  q->size = size;
  q->cap = cap;
  q->queued = 0;
  q->next = 0;
}

gadget_status_t pl_otaq_admit(pl_otaq_t *q, uint32_t offset, size_t len) {
  if (len == 0 || len > GADGET_FW_CHUNK_MAX || offset != q->next) {
    return GADGET_ERR_ARG;
  }
  if ((uint64_t)offset + len > q->size) {
    return GADGET_ERR_LIMIT;
  }
  if (q->queued + len > q->cap) {
    return GADGET_ERR_BUSY;
  }
  q->queued += (uint32_t)len;
  q->next += (uint32_t)len;
  return GADGET_OK;
}

/* Unused by hal_ota.c (see pl_otaq.h). */
void pl_otaq_unadmit(pl_otaq_t *q, size_t len) {
  q->next -= (uint32_t)len;
  pl_otaq_done(q, len);
}

void pl_otaq_done(pl_otaq_t *q, size_t len) {
  q->queued = len >= q->queued ? 0 : q->queued - (uint32_t)len;
}

pl_otaq_split_t pl_otaq_split(uint32_t staged, size_t len, bool last) {
  pl_otaq_split_t s = {0};
  uint32_t room = staged < GADGET_FW_CHUNK_MAX ? GADGET_FW_CHUNK_MAX - staged : 0;
  s.head = len < room ? (uint32_t)len : room;
  s.tail = (uint32_t)len - s.head;
  s.send_head = staged + s.head == GADGET_FW_CHUNK_MAX || (last && staged + s.head > 0);
  s.send_tail = last && s.tail > 0;
  return s;
}
