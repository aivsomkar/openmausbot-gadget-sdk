/* SPDX-License-Identifier: Apache-2.0 */
/* Test harness: splits each stdin line with the firmware's own
 * gadget_console_split (firmware/core/src/console.c) and prints the arguments
 * as one JSON array per line, or null for an unterminated quote or too many
 * arguments. Used by site/test/console-firmware.test.ts and
 * tools/console/test_omb_console.py. */
#include <stdio.h>
#include <string.h>

#include "gadget_console.h"
#include "gadget_util.h"

/* console.c's grammar calls these two util.c validators; the splitter never
 * does. Stubbing them keeps util.c and its crypto HAL out of this build. */
bool gadget_pair_code_valid(const char *code) {
  (void)code;
  return false;
}
size_t gadget_utf8_len(const char *s) { return strlen(s); }

static void put_json(const char *s) {
  putchar('"');
  for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
    if (*p == '"' || *p == '\\') {
      printf("\\%c", *p);
    } else if (*p < 0x20) {
      printf("\\u%04x", *p);
    } else {
      putchar(*p);
    }
  }
  putchar('"');
}

int main(void) {
  static char line[GADGET_CONSOLE_LINE_MAX];
  while (fgets(line, sizeof line, stdin) != NULL) {
    line[strcspn(line, "\r\n")] = '\0';
    char *argv[GADGET_CONSOLE_ARGV_MAX];
    int argc = gadget_console_split(line, argv, (int)GADGET_CONSOLE_ARGV_MAX);
    if (argc < 0 || argc > (int)GADGET_CONSOLE_ARGV_MAX) {
      puts("null");
      continue;
    }
    putchar('[');
    for (int i = 0; i < argc; i++) {
      if (i > 0) putchar(',');
      put_json(argv[i]);
    }
    puts("]");
  }
  return 0;
}
