/* SPDX-License-Identifier: Apache-2.0 */
/* Prints firmware/core's key tables as one JSON line for
 * tools/release/check-keys.ts. Compiled without GADGET_TEST_KEYS, exactly
 * as a release build compiles core (contract §2.13). */
#include <stdio.h>

#include "gadget_ota.h"

static void print_id(const char *id) {
  if (id == NULL) {
    fputs("null", stdout);
    return;
  }
  putchar('"');
  for (const char *p = id; *p != '\0'; p++) {
    unsigned char c = (unsigned char)*p;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
      putchar(c);
    } else {
      printf("\\u%04x", c);
    }
  }
  putchar('"');
}

static void print_table(const gadget_release_key_t *table, size_t count) {
  putchar('[');
  for (size_t i = 0; i < count; i++) {
    if (i > 0) putchar(',');
    fputs("{\"id\":", stdout);
    print_id(table[i].id);
    fputs(",\"pub\":\"", stdout);
    for (size_t j = 0; j < GADGET_PUBKEY_LEN; j++) printf("%02x", table[i].pub[j]);
    fputs("\"}", stdout);
  }
  putchar(']');
}

int main(void) {
  fputs("{\"release\":", stdout);
  print_table(gadget_release_keys, gadget_release_keys_count);
  fputs(",\"test\":", stdout);
  print_table(gadget_test_keys, gadget_test_keys_count);
  fputs("}\n", stdout);
  return 0;
}
