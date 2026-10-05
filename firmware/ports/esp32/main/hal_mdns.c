/* SPDX-License-Identifier: Apache-2.0 */
/* HAL mDNS group: browse _openmausbot._tcp (spec §5.6 `host auto`). The
 * queries block, so they run on their own task, all within timeout_ms
 * (core stops waiting 1 s later), and the result reaches core as one
 * GADGET_EV_MDNS. */
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gadget_hal.h"
#include "mdns.h"
#include "pl_mdns.h"
#include "port.h"

#define MDNS_RESULTS_MAX 16

static const char *TAG = "mdns";
static QueueHandle_t s_req;
static volatile bool s_busy;

/* hal_now_ms()'s clock. Driver tasks call no hal_* function (contract §2.1). */
static uint64_t now_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }

static void fill_ipv4(const mdns_result_t *r, pl_mdns_in_t *in, uint64_t deadline) {
  for (const mdns_ip_addr_t *a = r->addr; a != NULL; a = a->next) {
    if (a->addr.type == ESP_IPADDR_TYPE_V4) {
      memcpy(in->ipv4, &a->addr.u_addr.ip4.addr, 4); /* network order: a.b.c.d */
      in->has_ipv4 = true;
      return;
    }
  }
  /* No A record in the answer: ask for one, but only inside the deadline. */
  uint32_t wait_ms = pl_mdns_a_ms(now_ms(), deadline);
  esp_ip4_addr_t ip;
  if (r->hostname != NULL && wait_ms > 0 && mdns_query_a(r->hostname, wait_ms, &ip) == ESP_OK) {
    memcpy(in->ipv4, &ip.addr, 4);
    in->has_ipv4 = true;
  }
}

static void mdns_task(void *arg) {
  (void)arg;
  uint32_t timeout_ms;
  for (;;) {
    if (xQueueReceive(s_req, &timeout_ms, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    const uint64_t deadline = now_ms() + timeout_ms;
    gadget_mdns_host_t hosts[PL_MDNS_MAX_HOSTS];
    uint8_t count = 0;
    mdns_result_t *res = NULL;
    esp_err_t err = mdns_query_ptr("_openmausbot", "_tcp", pl_mdns_ptr_ms(timeout_ms), MDNS_RESULTS_MAX, &res);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "browse failed: %s", esp_err_to_name(err));
    }
    for (const mdns_result_t *r = res; err == ESP_OK && r != NULL; r = r->next) {
      pl_mdns_in_t in = {.instance = r->instance_name, .hostname = r->hostname, .port = r->port};
      fill_ipv4(r, &in, deadline);
      for (size_t i = 0; i < r->txt_count; i++) {
        if (pl_mdns_txt_is_id(r->txt[i].key) && r->txt[i].value != NULL) {
          in.txt_id = r->txt[i].value;
          in.txt_id_len = r->txt_value_len != NULL ? r->txt_value_len[i] : strlen(r->txt[i].value);
        }
      }
      gadget_mdns_host_t h;
      if (pl_mdns_host(&in, &h)) {
        count = pl_mdns_add(hosts, count, PL_MDNS_MAX_HOSTS, &h);
      }
    }
    if (res != NULL) {
      mdns_query_results_free(res);
    }
    /* Posted by the deadline: every query above stopped inside it. */
    gadget_event_t ev = {.type = GADGET_EV_MDNS};
    ev.u.mdns.hosts = hosts;
    ev.u.mdns.count = count;
    ev.u.mdns.ok = err == ESP_OK;
    port_post_event(&ev);
    s_busy = false;
  }
}

esp_err_t port_mdns_init(void) {
  esp_err_t err = mdns_init();
  if (err != ESP_OK) {
    return err;
  }
  s_req = xQueueCreate(1, sizeof(uint32_t));
  if (s_req == NULL) {
    return ESP_ERR_NO_MEM;
  }
  return xTaskCreatePinnedToCore(mdns_task, "mdns_browse", 4096, NULL, 3, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

gadget_status_t hal_mdns_browse(uint32_t timeout_ms) {
  if (s_busy) {
    return GADGET_ERR_BUSY;
  }
  s_busy = true;
  if (xQueueSend(s_req, &timeout_ms, 0) != pdTRUE) {
    s_busy = false;
    return GADGET_ERR_BUSY;
  }
  return GADGET_OK;
}
