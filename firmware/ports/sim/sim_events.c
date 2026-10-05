/* firmware/ports/sim/sim_events.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The simulator's event queue (sim_hal.h): any thread posts, the main loop
 * delivers in order before the next core_tick(). Payloads are deep-copied. */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "sim_hal.h"
#include "sim_internal.h"

typedef struct node {
  gadget_event_t ev;
  void *copy;
  struct node *next;
} node_t;

static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;
static node_t *s_head, *s_tail;

static void *dup_bytes(const void *src, size_t n) {
  void *p = malloc(n ? n : 1);
  if (p != NULL && n) memcpy(p, src, n);
  return p;
}

void sim_post_event(const gadget_event_t *ev) {
  node_t *n = calloc(1, sizeof *n);
  if (n == NULL) return;
  n->ev = *ev;
  switch (ev->type) {
    case GADGET_EV_MIC_FRAME:
      n->copy = dup_bytes(ev->u.mic.pcm, ev->u.mic.samples * sizeof(int16_t));
      n->ev.u.mic.pcm = n->copy;
      break;
    case GADGET_EV_WS_TEXT:
    case GADGET_EV_WS_BINARY:
      n->copy = dup_bytes(ev->u.ws.data, ev->u.ws.len);
      n->ev.u.ws.data = n->copy;
      break;
    case GADGET_EV_CONSOLE_LINE:
      if (ev->u.console.line != NULL) {
        n->copy = dup_bytes(ev->u.console.line, strlen(ev->u.console.line) + 1);
        n->ev.u.console.line = n->copy;
      }
      break;
    case GADGET_EV_WIFI_SCAN:
      n->copy = dup_bytes(ev->u.scan.aps, ev->u.scan.count * sizeof(gadget_wifi_ap_t));
      n->ev.u.scan.aps = n->copy;
      break;
    case GADGET_EV_MDNS:
      n->copy = dup_bytes(ev->u.mdns.hosts, ev->u.mdns.count * sizeof(gadget_mdns_host_t));
      n->ev.u.mdns.hosts = n->copy;
      break;
    default:
      break;
  }
  pthread_mutex_lock(&s_mu);
  if (s_tail) s_tail->next = n;
  else s_head = n;
  s_tail = n;
  pthread_mutex_unlock(&s_mu);
}

void sim_events_deliver(void) {
  pthread_mutex_lock(&s_mu);
  node_t *list = s_head;
  s_head = s_tail = NULL;
  pthread_mutex_unlock(&s_mu);
  while (list != NULL) {
    node_t *next = list->next;
    core_event(&list->ev);
    free(list->copy);
    free(list);
    list = next;
  }
}
