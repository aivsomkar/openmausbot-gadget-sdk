/* firmware/core/src/keys_release.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Release public keys (ids /^r[0-9]+$/) compiled into every build. Empty
 * until Omkar commits keys/release-r1.pub.b64; plan P2d then adds
 *   {"r1", {0x04, ...65 bytes...}}
 * and sets the count to 1. Release CI fails while the count is 0. */
#include "gadget_ota.h"

const gadget_release_key_t gadget_release_keys[] = {{NULL, {0}}};
const size_t gadget_release_keys_count = 0;
