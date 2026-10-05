/* SPDX-License-Identifier: Apache-2.0 */
/* Small pure helpers for the ESP32 port. */
#ifndef PL_UTIL_H
#define PL_UTIL_H

#include "gadget_types.h"

/* NVS keys are 1..15 bytes (gadget_hal.h storage group). */
bool pl_storage_key_ok(const char *key);

/* "ws://<host>:<port>/gadget". host is a DNS name or dotted IPv4:
 * 1..253 bytes of [A-Za-z0-9.-_]; no IPv6 literals. port 1..65535.
 * GADGET_ERR_ARG for a bad host or port, GADGET_ERR_LIMIT when cap is too small. */
gadget_status_t pl_ws_uri(char *out, size_t cap, const char *host, uint16_t port);

/* Copy at most cap-1 bytes of src, cutting on a UTF-8 code point boundary. */
void pl_utf8_trunc(char *dst, size_t cap, const char *src);

#endif /* PL_UTIL_H */
