/* firmware/ports/sim/sim_mdns.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* host auto in the simulator: browse _openmausbot._tcp with Apple's dns_sd
 * (macOS only; Linux needs --host). Each found instance is resolved (port,
 * TXT id=) and its IPv4 address looked up; one GADGET_EV_MDNS reports all of
 * them when the browse time is up. Driven from the main loop, never blocks. */
#include <string.h>
#include "gadget_hal.h"
#include "sim_hal.h"
#include "sim_internal.h"

#if defined(__APPLE__)
#include <arpa/inet.h>
#include <dns_sd.h>
#include <poll.h>

#define MAX_REFS 24

static struct {
  bool active;
  uint64_t deadline;
  DNSServiceRef refs[MAX_REFS];
  int n_refs;
  gadget_mdns_host_t hosts[8];
  uint16_t ports[8];
  uint8_t count;
} M;

static void add_ref(DNSServiceRef r) {
  if (M.n_refs < MAX_REFS) M.refs[M.n_refs++] = r;
  else DNSServiceRefDeallocate(r);
}

static void DNSSD_API on_addr(DNSServiceRef ref, DNSServiceFlags flags, uint32_t ifindex, DNSServiceErrorType err,
                              const char *hostname, const struct sockaddr *addr, uint32_t ttl, void *ctx) {
  (void)ref;
  (void)flags;
  (void)ifindex;
  (void)hostname;
  (void)ttl;
  gadget_mdns_host_t *h = ctx;
  if (err != kDNSServiceErr_NoError || addr == NULL || addr->sa_family != AF_INET || h->address[0] != '\0') return;
  char ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &((const struct sockaddr_in *)(const void *)addr)->sin_addr, ip, sizeof ip);
  uint16_t port = M.ports[h - M.hosts];
  snprintf(h->address, sizeof h->address, "%s:%u", ip, (unsigned)port);
}

static void DNSSD_API on_resolve(DNSServiceRef ref, DNSServiceFlags flags, uint32_t ifindex, DNSServiceErrorType err,
                                 const char *fullname, const char *target, uint16_t port_be, uint16_t txt_len,
                                 const unsigned char *txt, void *ctx) {
  (void)ref;
  (void)flags;
  (void)fullname;
  gadget_mdns_host_t *h = ctx;
  if (err != kDNSServiceErr_NoError) return;
  M.ports[h - M.hosts] = ntohs(port_be);
  uint8_t vlen = 0;
  const void *v = TXTRecordGetValuePtr(txt_len, txt, "id", &vlen);
  if (v != NULL && vlen <= GADGET_HOST_ID_LEN) {
    memcpy(h->id, v, vlen);
    h->id[vlen] = '\0';
  }
  DNSServiceRef a = NULL;
  if (DNSServiceGetAddrInfo(&a, 0, ifindex, kDNSServiceProtocol_IPv4, target, on_addr, h) == kDNSServiceErr_NoError) {
    add_ref(a);
  }
}

static void DNSSD_API on_browse(DNSServiceRef ref, DNSServiceFlags flags, uint32_t ifindex, DNSServiceErrorType err,
                                const char *name, const char *type, const char *domain, void *ctx) {
  (void)ref;
  (void)ctx;
  if (err != kDNSServiceErr_NoError || !(flags & kDNSServiceFlagsAdd) || M.count >= 8) return;
  for (uint8_t i = 0; i < M.count; i++) {
    if (strcmp(M.hosts[i].name, name) == 0) return; /* seen on another interface */
  }
  gadget_mdns_host_t *h = &M.hosts[M.count++];
  memset(h, 0, sizeof *h);
  snprintf(h->name, sizeof h->name, "%s", name);
  DNSServiceRef r = NULL;
  if (DNSServiceResolve(&r, 0, ifindex, name, type, domain, on_resolve, h) == kDNSServiceErr_NoError) add_ref(r);
}

static void finish(void) {
  gadget_mdns_host_t found[8];
  uint8_t n = 0;
  for (uint8_t i = 0; i < M.count; i++) {
    if (M.hosts[i].address[0] != '\0') found[n++] = M.hosts[i]; /* only fully resolved ones */
  }
  for (int i = 0; i < M.n_refs; i++) DNSServiceRefDeallocate(M.refs[i]);
  M.n_refs = 0;
  M.active = false;
  gadget_event_t ev = {.type = GADGET_EV_MDNS};
  ev.u.mdns.hosts = found;
  ev.u.mdns.count = n;
  ev.u.mdns.ok = true;
  sim_post_event(&ev);
}

gadget_status_t hal_mdns_browse(uint32_t timeout_ms) {
  if (M.active) return GADGET_ERR_BUSY;
  memset(&M, 0, sizeof M);
  DNSServiceRef b = NULL;
  if (DNSServiceBrowse(&b, 0, kDNSServiceInterfaceIndexAny, "_openmausbot._tcp", NULL, on_browse, NULL) !=
      kDNSServiceErr_NoError) {
    return GADGET_ERR_IO;
  }
  add_ref(b);
  M.active = true;
  M.deadline = hal_now_ms() + timeout_ms;
  return GADGET_OK;
}

void sim_mdns_poll(void) {
  if (!M.active) return;
  for (int i = 0; i < M.n_refs; i++) {
    struct pollfd p = {.fd = DNSServiceRefSockFD(M.refs[i]), .events = POLLIN};
    if (poll(&p, 1, 0) > 0 && (p.revents & POLLIN)) DNSServiceProcessResult(M.refs[i]);
  }
  if (hal_now_ms() >= M.deadline) finish();
}

#else /* Linux: no dns_sd without a running avahi-daemon; --host is required */

gadget_status_t hal_mdns_browse(uint32_t timeout_ms) {
  (void)timeout_ms;
  return GADGET_ERR_UNSUPPORTED;
}

void sim_mdns_poll(void) {}

#endif
