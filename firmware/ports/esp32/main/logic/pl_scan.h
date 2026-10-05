/* SPDX-License-Identifier: Apache-2.0 */
/* Wi-Fi scan result merging for the @omb scan line (gadget_events.h). */
#ifndef PL_SCAN_H
#define PL_SCAN_H

#include "gadget_events.h"

#define PL_SCAN_MAX 20u

/* Merge raw scan records into out[0..cap): hidden networks (empty SSID) are
 * dropped, each SSID keeps its strongest record, the result is sorted
 * strongest first (equal RSSI: SSID byte order). in and out must not
 * overlap; cap <= PL_SCAN_MAX. Returns the number written. */
uint8_t pl_scan_merge(const gadget_wifi_ap_t *in, size_t n, gadget_wifi_ap_t *out, uint8_t cap);

#endif /* PL_SCAN_H */
