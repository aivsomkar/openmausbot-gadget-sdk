/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_mdns.h"

#include <stdio.h>
#include <string.h>

#include "pl_util.h"

bool pl_mdns_txt_is_id(const char *key) {
  return key != NULL && (key[0] == 'i' || key[0] == 'I') && (key[1] == 'd' || key[1] == 'D') && key[2] == '\0';
}

static bool hex32(const char *s, size_t n) {
  if (s == NULL || n != GADGET_HOST_ID_LEN) {
    return false;
  }
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

bool pl_mdns_host(const pl_mdns_in_t *in, gadget_mdns_host_t *out) {
  if (!in->has_ipv4 || in->port == 0) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  const char *name = in->instance != NULL && in->instance[0] != '\0' ? in->instance
                     : in->hostname != NULL && in->hostname[0] != '\0' ? in->hostname
                                                                        : "MausBot";
  pl_utf8_trunc(out->name, sizeof(out->name), name);
  snprintf(out->address, sizeof(out->address), "%u.%u.%u.%u:%u", (unsigned)in->ipv4[0], (unsigned)in->ipv4[1],
           (unsigned)in->ipv4[2], (unsigned)in->ipv4[3], (unsigned)in->port);
  if (hex32(in->txt_id, in->txt_id_len)) {
    memcpy(out->id, in->txt_id, GADGET_HOST_ID_LEN);
    out->id[GADGET_HOST_ID_LEN] = '\0';
  }
  return true;
}

uint8_t pl_mdns_add(gadget_mdns_host_t *list, uint8_t count, uint8_t cap, const gadget_mdns_host_t *h) {
  for (uint8_t i = 0; i < count; i++) {
    if (strcmp(list[i].address, h->address) == 0) {
      return count;
    }
  }
  if (count >= cap) {
    return count;
  }
  list[count] = *h;
  return (uint8_t)(count + 1);
}

uint32_t pl_mdns_ptr_ms(uint32_t timeout_ms) { return timeout_ms > 2000u ? timeout_ms - 1000u : timeout_ms; }

uint32_t pl_mdns_a_ms(uint64_t now, uint64_t deadline) {
  if (now + 200u >= deadline) {
    return 0;
  }
  uint64_t left = deadline - now;
  return left < PL_MDNS_A_TIMEOUT_MS ? (uint32_t)left : PL_MDNS_A_TIMEOUT_MS;
}
