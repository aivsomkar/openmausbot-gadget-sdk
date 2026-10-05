/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include "pl_event.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_mic_frame_is_deep_copied(void) {
  int16_t pcm[GADGET_MIC_FRAME_SAMPLES];
  for (unsigned i = 0; i < GADGET_MIC_FRAME_SAMPLES; i++) pcm[i] = (int16_t)(i * 3 - 400);
  gadget_event_t src = {.type = GADGET_EV_MIC_FRAME};
  src.u.mic.pcm = pcm;
  src.u.mic.samples = GADGET_MIC_FRAME_SAMPLES;
  gadget_event_t dst;
  TEST_ASSERT_EQUAL(GADGET_OK, pl_event_copy(&dst, &src));
  TEST_ASSERT_TRUE(dst.u.mic.pcm != pcm);
  pcm[5] = 0; /* the copy must not follow later changes to the source */
  TEST_ASSERT_EQUAL_INT16(-385, dst.u.mic.pcm[5]);
  TEST_ASSERT_EQUAL_INT16_ARRAY(pcm + 6, dst.u.mic.pcm + 6, GADGET_MIC_FRAME_SAMPLES - 6);
  pl_event_free(&dst);
  TEST_ASSERT_NULL(dst.u.mic.pcm);
}

static void test_ws_text_including_empty(void) {
  const uint8_t text[] = "{\"op\":\"ready\"}";
  gadget_event_t src = {.type = GADGET_EV_WS_TEXT};
  src.u.ws.data = text;
  src.u.ws.len = sizeof(text) - 1;
  gadget_event_t dst;
  TEST_ASSERT_EQUAL(GADGET_OK, pl_event_copy(&dst, &src));
  TEST_ASSERT_TRUE(dst.u.ws.data != text);
  TEST_ASSERT_EQUAL_size_t(sizeof(text) - 1, dst.u.ws.len);
  TEST_ASSERT_EQUAL_MEMORY(text, dst.u.ws.data, sizeof(text) - 1);
  pl_event_free(&dst);

  gadget_event_t empty = {.type = GADGET_EV_WS_BINARY};
  empty.u.ws.data = NULL;
  empty.u.ws.len = 0;
  TEST_ASSERT_EQUAL(GADGET_OK, pl_event_copy(&dst, &empty));
  TEST_ASSERT_NOT_NULL(dst.u.ws.data);
  TEST_ASSERT_EQUAL_size_t(0, dst.u.ws.len);
  pl_event_free(&dst);
}

static void test_scan_mdns_console_payloads(void) {
  gadget_wifi_ap_t aps[2] = {{"Home", -40, GADGET_AUTH_WPA2}, {"Cafe", -80, GADGET_AUTH_OPEN}};
  gadget_event_t src = {.type = GADGET_EV_WIFI_SCAN};
  src.u.scan.aps = aps;
  src.u.scan.count = 2;
  src.u.scan.ok = true;
  gadget_event_t dst;
  TEST_ASSERT_EQUAL(GADGET_OK, pl_event_copy(&dst, &src));
  TEST_ASSERT_TRUE(dst.u.scan.aps != aps);
  TEST_ASSERT_EQUAL_STRING("Cafe", dst.u.scan.aps[1].ssid);
  TEST_ASSERT_TRUE(dst.u.scan.ok);
  pl_event_free(&dst);

  gadget_mdns_host_t hosts[1] = {{"Omkar's computer", "192.168.1.20:8810", "000102030405060708090a0b0c0d0e0f"}};
  gadget_event_t m = {.type = GADGET_EV_MDNS};
  m.u.mdns.hosts = hosts;
  m.u.mdns.count = 1;
  m.u.mdns.ok = true;
  TEST_ASSERT_EQUAL(GADGET_OK, pl_event_copy(&dst, &m));
  TEST_ASSERT_EQUAL_STRING("192.168.1.20:8810", dst.u.mdns.hosts[0].address);
  pl_event_free(&dst);

  char line[] = "wifi \"My Net\" \"pass word\"";
  gadget_event_t c = {.type = GADGET_EV_CONSOLE_LINE};
  c.u.console.line = line;
  TEST_ASSERT_EQUAL(GADGET_OK, pl_event_copy(&dst, &c));
  line[0] = 'X';
  TEST_ASSERT_EQUAL_STRING("wifi \"My Net\" \"pass word\"", dst.u.console.line);
  pl_event_free(&dst);
}

static void test_value_events_need_no_free(void) {
  gadget_event_t src = {.type = GADGET_EV_WIFI_STATE};
  src.u.wifi.state = GADGET_WIFI_CONNECTED;
  strcpy(src.u.wifi.ip, "10.0.0.7");
  gadget_event_t dst;
  TEST_ASSERT_EQUAL(GADGET_OK, pl_event_copy(&dst, &src));
  TEST_ASSERT_EQUAL_STRING("10.0.0.7", dst.u.wifi.ip);
  pl_event_free(&dst);
  TEST_ASSERT_EQUAL(0, dst.type);
  pl_event_free(&dst); /* a zeroed event frees nothing */
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_mic_frame_is_deep_copied);
  RUN_TEST(test_ws_text_including_empty);
  RUN_TEST(test_scan_mdns_console_payloads);
  RUN_TEST(test_value_events_need_no_free);
  return UNITY_END();
}
