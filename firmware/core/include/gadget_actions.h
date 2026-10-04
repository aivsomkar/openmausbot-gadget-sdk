/* firmware/core/include/gadget_actions.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Actions a gadget declares in hello and runs on `act` (spec §4.7, §5.10). */
#ifndef GADGET_ACTIONS_H
#define GADGET_ACTIONS_H

#include "cJSON.h"
#include "gadget_types.h"

typedef enum {
  GADGET_RISK_CONFIRM = 0,   /* "confirm": MausBot always asks the person first */
  GADGET_RISK_SAFE = 1       /* "safe" */
} gadget_risk_t;

/* Runs on the main thread when an `act` for this action arrives. Fill
 * `data` (an empty cJSON object owned by core; left empty → "data" omitted)
 * and return true, or write a short reason into `error` (≤ error_cap - 1
 * bytes, Latin-1) and return false. Must not block for more than 100 ms.
 * Core sends exactly one act.result per act. */
typedef bool (*gadget_action_handler_t)(const cJSON *args, cJSON *data, char *error, size_t error_cap);

/* Declare an action. Call after core_init() and before the first
 * core_tick(); an action registered later is declared in the next hello.
 *   name:        /^[a-z][a-z0-9_.-]{0,31}$/
 *   description: 1..200 characters
 *   params_schema_json: a JSON Schema object as text (≤ 1024 bytes when
 *                re-serialized); NULL → {"type":"object","properties":{}}
 *   risk:        GADGET_RISK_SAFE or GADGET_RISK_CONFIRM
 * Returns GADGET_OK, GADGET_ERR_ARG (bad name, invalid JSON, NULL handler),
 * GADGET_ERR_STATE (duplicate name) or GADGET_ERR_LIMIT (more than 16
 * actions, a field too long, or the hello would exceed 16 KiB). */
gadget_status_t gadget_action_register(const char *name, const char *description, const char *params_schema_json,
                                       gadget_risk_t risk, gadget_action_handler_t handler);

/* Tell MausBot that something happened on the gadget: sends
 * `event {name, data?}` (spec §4.7). Main thread only. Events are
 * informational: MausBot keeps the last 10 per gadget and lists them to
 * bots as `recent_events`, and nothing reacts to them. Nothing is queued,
 * so an event sent while busy is lost.
 *   name: /^[a-z][a-z0-9_.-]{0,31}$/ (the action-name rule)
 *   data: any JSON value (copied; the caller keeps ownership), ≤ 1024
 *         bytes when serialized compactly; NULL → "data" omitted
 * Returns GADGET_OK, GADGET_ERR_ARG (bad name), GADGET_ERR_LIMIT (data too
 * long), GADGET_ERR_NO_MEM, GADGET_ERR_BUSY (no ready session: offline,
 * mid-handshake or before core_init()) or the socket's error. The name and
 * size checks come first, so they fail the same way offline. */
gadget_status_t gadget_event_send(const char *name, const cJSON *data);

#endif /* GADGET_ACTIONS_H */
