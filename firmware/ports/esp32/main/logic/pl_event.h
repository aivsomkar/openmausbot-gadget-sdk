/* SPDX-License-Identifier: Apache-2.0 */
/* Deep copies of gadget events. Driver tasks copy an event before queueing
 * it for the main thread, because pointers inside a gadget_event_t are only
 * borrowed for one core_event() call (gadget_events.h). */
#ifndef PL_EVENT_H
#define PL_EVENT_H

#include "gadget_events.h"

/* Copy *src into *dst and duplicate every pointer payload (mic.pcm, ws.data,
 * scan.aps, mdns.hosts, console.line). On GADGET_ERR_NO_MEM *dst is zeroed
 * and owns nothing. */
gadget_status_t pl_event_copy(gadget_event_t *dst, const gadget_event_t *src);
/* Free what pl_event_copy() allocated and zero *ev. Safe on a zeroed event. */
void pl_event_free(gadget_event_t *ev);

#endif /* PL_EVENT_H */
