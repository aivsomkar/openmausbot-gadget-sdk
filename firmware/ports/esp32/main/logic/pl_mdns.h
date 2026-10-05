/* SPDX-License-Identifier: Apache-2.0 */
/* Turns one mDNS browse answer for _openmausbot._tcp into a
 * gadget_mdns_host_t (gadget_events.h). */
#ifndef PL_MDNS_H
#define PL_MDNS_H

#include "gadget_events.h"

#define PL_MDNS_MAX_HOSTS 8u
#define PL_MDNS_A_TIMEOUT_MS 1000u   /* longest single address lookup */

typedef struct {
  const char *instance;     /* service instance name, NULL when absent */
  const char *hostname;     /* target host, NULL when absent */
  uint16_t port;
  bool has_ipv4;
  uint8_t ipv4[4];          /* network order: a.b.c.d */
  const char *txt_id;       /* value of TXT key "id", NULL when absent */
  size_t txt_id_len;
} pl_mdns_in_t;

/* DNS-SD TXT keys compare case-insensitively. */
bool pl_mdns_txt_is_id(const char *key);
/* false when the answer cannot be used (no IPv4 address or port 0).
 * name = instance, else hostname, else "MausBot" (cut to 63 bytes on a
 * UTF-8 boundary); address = "a.b.c.d:port"; id = the TXT id when it is
 * exactly 32 lowercase hex characters, else "". */
bool pl_mdns_host(const pl_mdns_in_t *in, gadget_mdns_host_t *out);
/* Append h unless its address is already listed or count == cap.
 * Returns the new count. */
uint8_t pl_mdns_add(gadget_mdns_host_t *list, uint8_t count, uint8_t cap, const gadget_mdns_host_t *h);
/* A browse of timeout_ms ends by its deadline (contract §2.5). The PTR query
 * gets timeout_ms - 1000 when timeout_ms > 2000 (the rest is for address
 * lookups), else all of it. */
uint32_t pl_mdns_ptr_ms(uint32_t timeout_ms);
/* Timeout for one address lookup started at `now`: at most
 * PL_MDNS_A_TIMEOUT_MS and never past `deadline`; 0 (skip the lookup) when
 * fewer than 200 ms are left. */
uint32_t pl_mdns_a_ms(uint64_t now, uint64_t deadline);

#endif /* PL_MDNS_H */
