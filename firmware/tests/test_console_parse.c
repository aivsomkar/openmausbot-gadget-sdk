/* firmware/tests/test_console_parse.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Console line assembly, quoting and grammar (core/src/console.c). */
#include <string.h>
#include "gadget_console.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static char g_lines[8][64];
static int g_count;
static int g_nulls;

static void on_line(const char *line, void *ctx) {
  (void)ctx;
  if (line == NULL) {
    g_nulls++;
    return;
  }
  snprintf(g_lines[g_count++ % 8], sizeof g_lines[0], "%s", line);
}

static void feed(gadget_linebuf_t *lb, const char *s) { gadget_linebuf_feed(lb, s, strlen(s), on_line, NULL); }

static void test_linebuf_handles_cr_lf_crlf(void) {
  static gadget_linebuf_t lb;
  g_count = g_nulls = 0;
  gadget_linebuf_init(&lb);
  feed(&lb, "status\r\nscan\nreboot\rlog on");
  TEST_ASSERT_EQUAL_INT(3, g_count);
  TEST_ASSERT_EQUAL_STRING("status", g_lines[0]);
  TEST_ASSERT_EQUAL_STRING("scan", g_lines[1]);
  TEST_ASSERT_EQUAL_STRING("reboot", g_lines[2]);
  feed(&lb, "\r");   /* completes "log on"; the LF may follow in the next chunk */
  feed(&lb, "\n");   /* ... and is not a second, empty line */
  TEST_ASSERT_EQUAL_INT(4, g_count);
  TEST_ASSERT_EQUAL_STRING("log on", g_lines[3]);
  feed(&lb, "\n");   /* a lone LF is an empty line */
  TEST_ASSERT_EQUAL_INT(5, g_count);
  TEST_ASSERT_EQUAL_STRING("", g_lines[4]);
}

static void test_linebuf_drops_overlong_lines(void) {
  static gadget_linebuf_t lb;
  static char big[GADGET_CONSOLE_LINE_MAX + 10];
  g_count = g_nulls = 0;
  gadget_linebuf_init(&lb);
  memset(big, 'x', sizeof big - 1);
  big[sizeof big - 1] = '\0';
  feed(&lb, big);
  feed(&lb, "\nstatus\n");
  TEST_ASSERT_EQUAL_INT(1, g_nulls);
  TEST_ASSERT_EQUAL_INT(1, g_count);
  TEST_ASSERT_EQUAL_STRING("status", g_lines[0]);
  /* exactly LINE_MAX - 1 bytes still fits */
  g_count = 0;
  big[GADGET_CONSOLE_LINE_MAX - 1] = '\0';
  feed(&lb, big);
  feed(&lb, "\n");
  TEST_ASSERT_EQUAL_INT(1, g_count);
  TEST_ASSERT_EQUAL_INT(1, g_nulls);
}

static void test_split_quotes_and_escapes(void) {
  char line[] = "wifi \"My Home\" \"p\\\"w\\\\d\" a\\ b \"\" x\"y z\"";
  char *argv[GADGET_CONSOLE_ARGV_MAX];
  int argc = gadget_console_split(line, argv, GADGET_CONSOLE_ARGV_MAX);
  TEST_ASSERT_EQUAL_INT(6, argc);
  TEST_ASSERT_EQUAL_STRING("wifi", argv[0]);
  TEST_ASSERT_EQUAL_STRING("My Home", argv[1]);
  TEST_ASSERT_EQUAL_STRING("p\"w\\d", argv[2]);
  TEST_ASSERT_EQUAL_STRING("a b", argv[3]);
  TEST_ASSERT_EQUAL_STRING("", argv[4]);
  TEST_ASSERT_EQUAL_STRING("xy z", argv[5]);
  char other[] = "  say \t hi\\n  ";
  argc = gadget_console_split(other, argv, GADGET_CONSOLE_ARGV_MAX);
  TEST_ASSERT_EQUAL_INT(2, argc);
  TEST_ASSERT_EQUAL_STRING("hi\\n", argv[1]); /* other escapes keep their backslash */
  char open_quote[] = "name \"Desk";
  TEST_ASSERT_EQUAL_INT(-1, gadget_console_split(open_quote, argv, GADGET_CONSOLE_ARGV_MAX));
  char many[] = "a b c d";
  TEST_ASSERT_EQUAL_INT(3, gadget_console_split(many, argv, 2)); /* argv_max + 1: too many */
  char empty[] = "   ";
  TEST_ASSERT_EQUAL_INT(0, gadget_console_split(empty, argv, GADGET_CONSOLE_ARGV_MAX));
}

static gadget_console_cmd_t parse(const char *text, gadget_console_parsed_t *p) {
  static char buf[GADGET_CONSOLE_LINE_MAX];
  strncpy(buf, text, sizeof buf - 1);
  buf[sizeof buf - 1] = '\0';
  return gadget_console_parse(buf, p);
}

static void test_parse_commands(void) {
  gadget_console_parsed_t p;
  TEST_ASSERT_EQUAL_INT(GC_EMPTY, parse("", &p));
  TEST_ASSERT_EQUAL_INT(GC_EMPTY, parse("   ", &p));
  TEST_ASSERT_EQUAL_INT(GC_WIFI, parse("wifi \"My Home\" \"secret123\"", &p));
  TEST_ASSERT_EQUAL_STRING("My Home", p.a);
  TEST_ASSERT_EQUAL_STRING("secret123", p.b);
  TEST_ASSERT_EQUAL_INT(GC_WIFI, parse("wifi Cafe \"\"", &p));
  TEST_ASSERT_EQUAL_STRING("", p.b);
  TEST_ASSERT_EQUAL_INT(GC_WIFI, parse("wifi Lab 0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789abcdef", &p));
  TEST_ASSERT_EQUAL_INT(GC_SCAN, parse("scan", &p));
  TEST_ASSERT_EQUAL_INT(GC_HOST_AUTO, parse("host auto", &p));
  TEST_ASSERT_EQUAL_INT(GC_HOST_SET, parse("host 192.168.1.20", &p));
  TEST_ASSERT_EQUAL_STRING("192.168.1.20", p.a);
  TEST_ASSERT_EQUAL_UINT16(8810, p.port);
  TEST_ASSERT_EQUAL_INT(GC_HOST_SET, parse("host omkars-mac.local:9000", &p));
  TEST_ASSERT_EQUAL_STRING("omkars-mac.local", p.a);
  TEST_ASSERT_EQUAL_UINT16(9000, p.port);
  TEST_ASSERT_EQUAL_INT(GC_PAIR, parse("pair 123456", &p));
  TEST_ASSERT_EQUAL_STRING("123456", p.a);
  TEST_ASSERT_EQUAL_INT(GC_NAME, parse("name \"  Desk Maus  \"", &p));
  TEST_ASSERT_EQUAL_STRING("Desk Maus", p.a);
  TEST_ASSERT_EQUAL_INT(GC_SAY, parse("say \"What's on today?\"", &p));
  TEST_ASSERT_EQUAL_STRING("What's on today?", p.a);
  TEST_ASSERT_EQUAL_INT(GC_STATUS, parse("status", &p));
  TEST_ASSERT_EQUAL_INT(GC_LOG_OFF, parse("log off", &p));
  TEST_ASSERT_EQUAL_INT(GC_LOG_ON, parse("log on", &p));
  TEST_ASSERT_EQUAL_INT(GC_FORGET, parse("forget", &p));
  TEST_ASSERT_EQUAL_INT(GC_REBOOT, parse("reboot", &p));
}

static void test_parse_errors(void) {
  gadget_console_parsed_t p;
  TEST_ASSERT_EQUAL_INT(GC_UNKNOWN, parse("Status", &p)); /* case-sensitive */
  TEST_ASSERT_EQUAL_STRING("Status", p.cmd_name);
  TEST_ASSERT_EQUAL_STRING("unknown command; try status", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("pair 12345", &p));
  TEST_ASSERT_EQUAL_STRING("pair", p.cmd_name);
  TEST_ASSERT_EQUAL_STRING("pair needs a six-digit code", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("wifi Home short", &p));
  TEST_ASSERT_EQUAL_STRING("the password must be empty, 8-63 characters or 64 hex digits", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("wifi \"\" password1", &p));
  TEST_ASSERT_EQUAL_STRING("the Wi-Fi name must be 1-32 bytes", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("wifi \"123456789012345678901234567890123\" password1", &p));
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("wifi Home", &p));
  TEST_ASSERT_EQUAL_STRING("wifi needs \"<ssid>\" \"<password>\"", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("host 1.2.3.4:0", &p));
  TEST_ASSERT_EQUAL_STRING("the port must be 1-65535", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("host 1.2.3.4:70000", &p));
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("host fe80::1", &p));
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("host bad_name", &p));
  TEST_ASSERT_EQUAL_STRING("host needs auto or <address>[:port]", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("name \"   \"", &p));
  TEST_ASSERT_EQUAL_STRING("name needs 1-32 characters", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("name 123456789012345678901234567890123", &p));
  TEST_ASSERT_EQUAL_INT(GC_NAME, parse("name \xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
                                      "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
                                      "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
                                      "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9",
                                      &p)); /* 32 code points, 64 bytes */
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("say \"\"", &p));
  TEST_ASSERT_EQUAL_STRING("say needs 1-2000 characters", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("log maybe", &p));
  TEST_ASSERT_EQUAL_STRING("log needs on or off", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("status now", &p));
  TEST_ASSERT_EQUAL_STRING("status takes no arguments", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("name \"Desk", &p));
  TEST_ASSERT_EQUAL_STRING("", p.cmd_name);
  TEST_ASSERT_EQUAL_STRING("unterminated quote", p.error);
}

static void test_say_accepts_the_longest_line(void) {
  /* 2000 two-byte code points, quoted: the longest valid say line fits */
  static char line[GADGET_CONSOLE_LINE_MAX];
  size_t o = 0;
  memcpy(line, "say \"", 5);
  o = 5;
  for (int i = 0; i < 2000; i++) {
    line[o++] = (char)0xc3;
    line[o++] = (char)0xa9;
  }
  line[o++] = '"';
  line[o] = '\0';
  gadget_console_parsed_t p;
  TEST_ASSERT_EQUAL_INT(GC_SAY, gadget_console_parse(line, &p));
  line[o - 1] = (char)0xc3; /* one more code point: 2001 */
  line[o++] = (char)0xa9;
  line[o++] = '"';
  line[o] = '\0';
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, gadget_console_parse(line, &p));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_linebuf_handles_cr_lf_crlf);
  RUN_TEST(test_linebuf_drops_overlong_lines);
  RUN_TEST(test_split_quotes_and_escapes);
  RUN_TEST(test_parse_commands);
  RUN_TEST(test_parse_errors);
  RUN_TEST(test_say_accepts_the_longest_line);
  return UNITY_END();
}
