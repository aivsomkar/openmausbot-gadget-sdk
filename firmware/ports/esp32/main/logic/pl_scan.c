/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_scan.h"

#include <string.h>

static bool before(const gadget_wifi_ap_t *a, const gadget_wifi_ap_t *b) {
  if (a->rssi != b->rssi) {
    return a->rssi > b->rssi;
  }
  return strcmp(a->ssid, b->ssid) < 0;
}

/* Move out[i] towards the front until the order holds again. */
static void bubble_up(gadget_wifi_ap_t *out, uint8_t i) {
  while (i > 0 && before(&out[i], &out[i - 1])) {
    gadget_wifi_ap_t t = out[i - 1];
    out[i - 1] = out[i];
    out[i] = t;
    i--;
  }
}

uint8_t pl_scan_merge(const gadget_wifi_ap_t *in, size_t n, gadget_wifi_ap_t *out, uint8_t cap) {
  uint8_t count = 0;
  if (cap > PL_SCAN_MAX) {
    cap = PL_SCAN_MAX;
  }
  for (size_t k = 0; k < n; k++) {
    gadget_wifi_ap_t ap = in[k];
    ap.ssid[sizeof(ap.ssid) - 1] = '\0';
    if (ap.ssid[0] == '\0') {
      continue;
    }
    uint8_t found = count;
    for (uint8_t i = 0; i < count; i++) {
      if (strcmp(out[i].ssid, ap.ssid) == 0) {
        found = i;
        break;
      }
    }
    if (found < count) {
      if (ap.rssi > out[found].rssi) {
        out[found] = ap;
        bubble_up(out, found);
      }
      continue;
    }
    if (count < cap) {
      out[count] = ap;
      bubble_up(out, count);
      count++;
    } else if (count > 0 && before(&ap, &out[count - 1])) {
      out[count - 1] = ap;
      bubble_up(out, (uint8_t)(count - 1));
    }
  }
  return count;
}
