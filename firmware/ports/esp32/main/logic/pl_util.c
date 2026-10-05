/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_util.h"

#include <stdio.h>
#include <string.h>

bool pl_storage_key_ok(const char *key) {
  if (key == NULL) {
    return false;
  }
  size_t n = strlen(key);
  return n >= 1 && n <= 15;
}

static bool host_char_ok(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
         c == '-' || c == '_';
}

gadget_status_t pl_ws_uri(char *out, size_t cap, const char *host, uint16_t port) {
  if (host == NULL || port == 0) {
    return GADGET_ERR_ARG;
  }
  size_t n = strlen(host);
  if (n == 0 || n > 253) {
    return GADGET_ERR_ARG;
  }
  for (size_t i = 0; i < n; i++) {
    if (!host_char_ok(host[i])) {
      return GADGET_ERR_ARG;
    }
  }
  int w = snprintf(out, cap, "ws://%s:%u%s", host, (unsigned)port, GADGET_WS_PATH);
  if (w < 0 || (size_t)w >= cap) {
    return GADGET_ERR_LIMIT;
  }
  return GADGET_OK;
}

void pl_utf8_trunc(char *dst, size_t cap, const char *src) {
  if (cap == 0) {
    return;
  }
  size_t n = strlen(src);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) {
      n--;
    }
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}
