/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_event.h"

#include <stdlib.h>
#include <string.h>

static void *dup_bytes(const void *src, size_t len) {
  void *p = malloc(len ? len : 1);
  if (p != NULL && len > 0) {
    memcpy(p, src, len);
  }
  return p;
}

gadget_status_t pl_event_copy(gadget_event_t *dst, const gadget_event_t *src) {
  void *p = NULL;
  *dst = *src;
  switch (src->type) {
    case GADGET_EV_MIC_FRAME:
      p = dup_bytes(src->u.mic.pcm, (size_t)src->u.mic.samples * sizeof(int16_t));
      dst->u.mic.pcm = p;
      break;
    case GADGET_EV_WS_TEXT:
    case GADGET_EV_WS_BINARY:
      p = dup_bytes(src->u.ws.data, src->u.ws.len);
      dst->u.ws.data = p;
      break;
    case GADGET_EV_WIFI_SCAN:
      p = dup_bytes(src->u.scan.aps, (size_t)src->u.scan.count * sizeof(gadget_wifi_ap_t));
      dst->u.scan.aps = p;
      break;
    case GADGET_EV_MDNS:
      p = dup_bytes(src->u.mdns.hosts, (size_t)src->u.mdns.count * sizeof(gadget_mdns_host_t));
      dst->u.mdns.hosts = p;
      break;
    case GADGET_EV_CONSOLE_LINE:
      p = dup_bytes(src->u.console.line, strlen(src->u.console.line) + 1);
      dst->u.console.line = p;
      break;
    default:
      return GADGET_OK;
  }
  if (p == NULL) {
    memset(dst, 0, sizeof(*dst));
    return GADGET_ERR_NO_MEM;
  }
  return GADGET_OK;
}

void pl_event_free(gadget_event_t *ev) {
  switch (ev->type) {
    case GADGET_EV_MIC_FRAME: free((void *)ev->u.mic.pcm); break;
    case GADGET_EV_WS_TEXT:
    case GADGET_EV_WS_BINARY: free((void *)ev->u.ws.data); break;
    case GADGET_EV_WIFI_SCAN: free((void *)ev->u.scan.aps); break;
    case GADGET_EV_MDNS: free((void *)ev->u.mdns.hosts); break;
    case GADGET_EV_CONSOLE_LINE: free((void *)ev->u.console.line); break;
    default: break;
  }
  memset(ev, 0, sizeof(*ev));
}
