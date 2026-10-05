/* firmware/ports/sim/sim_ws.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The simulator's WebSocket HAL (spec §5.7). --host script uses the
 * scripted network (sim_net_script.c). Otherwise: our own non-blocking TCP
 * connect and HTTP/1.1 upgrade (RFC 6455 §4: no Origin header, subprotocol
 * openmausbot-gadget.1, Sec-WebSocket-Accept checked), then wslay 1.1.1 for
 * framing, fragments, ping replies and the close handshake. One connection at
 * a time; exactly one GADGET_EV_WS_CLOSED follows every successful open. */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wslay/wslay.h>
#include "gadget_hal.h"
#include "gadget_util.h"
#include "psa/crypto.h"
#include "sim_hal.h"
#include "sim_internal.h"

#define TAG "ws"
#define CONNECT_TIMEOUT_MS 5000u
#define CLOSE_TIMEOUT_MS 2000u
#define RESPONSE_MAX 4096u
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

typedef enum { WS_IDLE = 0, WS_CONNECTING, WS_HANDSHAKE, WS_OPEN, WS_CLOSING } ws_state_t;

static struct {
  ws_state_t st;
  int fd;
  uint64_t deadline;
  char key[32];
  char req[512];
  size_t req_len, req_sent;
  char resp[RESPONSE_MAX + 1];
  size_t resp_len;
  uint8_t extra[RESPONSE_MAX]; /* bytes that followed the 101 response */
  size_t extra_len, extra_pos;
  wslay_event_context_ptr ctx;
  uint16_t close_code;
} W = {.fd = -1};

bool sim_net_scripted(void) { return g_sim.host != NULL && strcmp(g_sim.host, "script") == 0; }

static void post_simple(gadget_event_type_t type) {
  gadget_event_t ev = {.type = type};
  sim_post_event(&ev);
}

/* End the connection and report it, once. */
static void finish(uint16_t code) {
  if (W.ctx != NULL) wslay_event_context_free(W.ctx);
  if (W.fd >= 0) close(W.fd);
  W.ctx = NULL;
  W.fd = -1;
  W.st = WS_IDLE;
  gadget_event_t ev = {.type = GADGET_EV_WS_CLOSED};
  ev.u.closed.code = code;
  sim_post_event(&ev);
}

/* ---- wslay callbacks ----------------------------------------------------------- */

static ssize_t on_recv(wslay_event_context_ptr ctx, uint8_t *buf, size_t len, int flags, void *ud) {
  (void)flags;
  (void)ud;
  if (W.extra_pos < W.extra_len) { /* frames that arrived with the upgrade response */
    size_t n = W.extra_len - W.extra_pos < len ? W.extra_len - W.extra_pos : len;
    memcpy(buf, W.extra + W.extra_pos, n);
    W.extra_pos += n;
    return (ssize_t)n;
  }
  ssize_t r;
  while ((r = recv(W.fd, buf, len, 0)) < 0 && errno == EINTR) {
  }
  if (r < 0) {
    wslay_event_set_error(ctx, (errno == EAGAIN || errno == EWOULDBLOCK) ? WSLAY_ERR_WOULDBLOCK
                                                                         : WSLAY_ERR_CALLBACK_FAILURE);
    return -1;
  }
  if (r == 0) {
    wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
    return -1;
  }
  return r;
}

/* send() that never raises SIGPIPE: MSG_NOSIGNAL on Linux, SO_NOSIGPIPE
 * (set in hal_ws_open) on macOS. Every send on the socket goes through here. */
static ssize_t send_nosig(const void *data, size_t len) {
  ssize_t r;
#if defined(MSG_NOSIGNAL)
  while ((r = send(W.fd, data, len, MSG_NOSIGNAL)) < 0 && errno == EINTR) {
  }
#else
  while ((r = send(W.fd, data, len, 0)) < 0 && errno == EINTR) {
  }
#endif
  return r;
}

static ssize_t on_send(wslay_event_context_ptr ctx, const uint8_t *data, size_t len, int flags, void *ud) {
  (void)flags;
  (void)ud;
  ssize_t r = send_nosig(data, len);
  if (r < 0) {
    wslay_event_set_error(ctx, (errno == EAGAIN || errno == EWOULDBLOCK) ? WSLAY_ERR_WOULDBLOCK
                                                                         : WSLAY_ERR_CALLBACK_FAILURE);
    return -1;
  }
  return r;
}

static int on_mask(wslay_event_context_ptr ctx, uint8_t *buf, size_t len, void *ud) {
  (void)ctx;
  (void)ud;
  return hal_crypto_random(buf, len) == GADGET_OK ? 0 : -1;
}

static void on_msg(wslay_event_context_ptr ctx, const struct wslay_event_on_msg_recv_arg *a, void *ud) {
  (void)ctx;
  (void)ud;
  gadget_event_t ev = {.type = GADGET_EV_WS_CONTROL};
  switch (a->opcode) {
    case WSLAY_TEXT_FRAME:
    case WSLAY_BINARY_FRAME:
      if (a->opcode == WSLAY_BINARY_FRAME && a->msg_length > GADGET_BINARY_FRAME_MAX) {
        hal_log(GADGET_LOG_WARN, TAG, "dropped a %zu-byte binary message", a->msg_length);
        break; /* the HAL delivers binary messages of at most 8 KiB */
      }
      ev.type = a->opcode == WSLAY_TEXT_FRAME ? GADGET_EV_WS_TEXT : GADGET_EV_WS_BINARY;
      ev.u.ws.data = a->msg;
      ev.u.ws.len = a->msg_length;
      sim_post_event(&ev);
      break;
    case WSLAY_CONNECTION_CLOSE:
      W.close_code = a->status_code; /* wslay answers the close; finish() reports it */
      break;
    default: /* ping (wslay has queued the pong) or pong: liveness only */
      sim_post_event(&ev);
      break;
  }
}

/* ---- the upgrade -------------------------------------------------------------------- */

static const char *header(const char *name) {
  size_t n = strlen(name);
  for (const char *p = strstr(W.resp, "\r\n"); p != NULL; p = strstr(p + 2, "\r\n")) {
    if (strncasecmp(p + 2, name, n) == 0 && p[2 + n] == ':') {
      const char *v = p + 3 + n;
      while (*v == ' ') v++;
      return v;
    }
  }
  return NULL;
}

static bool header_is(const char *name, const char *want) {
  const char *v = header(name);
  return v != NULL && strncasecmp(v, want, strlen(want)) == 0 &&
         (v[strlen(want)] == '\r' || v[strlen(want)] == ' ');
}

/* True when the header's comma-separated value lists token, in any case. */
static bool header_has_token(const char *name, const char *token) {
  const char *v = header(name);
  if (v == NULL) return false;
  size_t n = strlen(token);
  const char *end = v + strcspn(v, "\r\n");
  for (const char *p = v; p < end;) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == ',')) p++;
    const char *q = p;
    while (q < end && *q != ',') q++;
    const char *e = q;
    while (e > p && (e[-1] == ' ' || e[-1] == '\t')) e--;
    if ((size_t)(e - p) == n && strncasecmp(p, token, n) == 0) return true;
    p = q;
  }
  return false;
}

/* RFC 6455 §4.1: the client fails the connection unless every check holds.
 * No extension was asked for (spec §4.1), so the response may name none. */
static bool response_ok(void) {
  if (strncmp(W.resp, "HTTP/1.1 101", 12) != 0) return false;
  char text[96];
  snprintf(text, sizeof text, "%s%s", W.key, WS_GUID);
  uint8_t sha1[20];
  size_t n = 0;
  if (psa_hash_compute(PSA_ALG_SHA_1, (const uint8_t *)text, strlen(text), sha1, sizeof sha1, &n) != PSA_SUCCESS) {
    return false;
  }
  char accept[32];
  gadget_b64_encode(accept, sizeof accept, sha1, sizeof sha1);
  return header_is("Upgrade", "websocket") && header_has_token("Connection", "upgrade") &&
         header_is("Sec-WebSocket-Accept", accept) && header_is("Sec-WebSocket-Protocol", GADGET_SUBPROTOCOL) &&
         header("Sec-WebSocket-Extensions") == NULL;
}

static void start_ws(void) {
  struct wslay_event_callbacks cbs = {on_recv, on_send, on_mask, NULL, NULL, NULL, on_msg};
  if (wslay_event_context_client_init(&W.ctx, &cbs, NULL) != 0) {
    finish(0);
    return;
  }
  wslay_event_config_set_max_recv_msg_length(W.ctx, GADGET_TEXT_FRAME_MAX);
  W.st = WS_OPEN;
  W.close_code = 0;
  post_simple(GADGET_EV_WS_OPEN);
}

static void handshake_io(short revents) {
  if ((revents & POLLOUT) && W.req_sent < W.req_len) {
    ssize_t r = send_nosig(W.req + W.req_sent, W.req_len - W.req_sent);
    if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      finish(0);
      return;
    }
    if (r > 0) W.req_sent += (size_t)r;
  }
  if (!(revents & (POLLIN | POLLHUP | POLLERR))) return;
  ssize_t r = recv(W.fd, W.resp + W.resp_len, RESPONSE_MAX - W.resp_len, 0);
  if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
    hal_log(GADGET_LOG_WARN, TAG, "the host closed during the upgrade");
    finish(0);
    return;
  }
  if (r < 0) return;
  W.resp_len += (size_t)r;
  W.resp[W.resp_len] = '\0';
  char *end = strstr(W.resp, "\r\n\r\n");
  if (end == NULL) {
    if (W.resp_len >= RESPONSE_MAX) finish(0);
    return;
  }
  size_t head = (size_t)(end - W.resp) + 4;
  W.extra_len = W.resp_len - head;
  W.extra_pos = 0;
  memcpy(W.extra, W.resp + head, W.extra_len);
  W.resp[head] = '\0';
  if (!response_ok()) {
    char first[96];
    snprintf(first, sizeof first, "%.*s", (int)strcspn(W.resp, "\r"), W.resp);
    hal_log(GADGET_LOG_WARN, TAG, "upgrade refused: %s", first);
    finish(0);
    return;
  }
  start_ws();
}

/* ---- the HAL ------------------------------------------------------------------------------ */

gadget_status_t hal_ws_open(const char *host, uint16_t port) {
  if (sim_net_scripted()) return sim_net_script_ws_open();
  if (W.st != WS_IDLE) return GADGET_ERR_BUSY;
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  char port_s[12];
  snprintf(port_s, sizeof port_s, "%u", (unsigned)port);
  if (getaddrinfo(host, port_s, &hints, &res) != 0 || res == NULL) {
    hal_log(GADGET_LOG_WARN, TAG, "cannot resolve %s", host);
    return GADGET_ERR_IO;
  }
  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) {
    freeaddrinfo(res);
    return GADGET_ERR_IO;
  }
  fcntl(fd, F_SETFD, FD_CLOEXEC); /* never leaks into the restart's execv */
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
#if defined(SO_NOSIGPIPE)
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
  int rc = connect(fd, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);
  if (rc != 0 && errno != EINPROGRESS) {
    close(fd);
    return GADGET_ERR_IO;
  }
  uint8_t nonce[16];
  hal_crypto_random(nonce, sizeof nonce);
  gadget_b64_encode(W.key, sizeof W.key, nonce, sizeof nonce);
  W.req_len = (size_t)snprintf(W.req, sizeof W.req,
                               "GET " GADGET_WS_PATH " HTTP/1.1\r\nHost: %s:%u\r\nUpgrade: websocket\r\n"
                               "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n"
                               "Sec-WebSocket-Protocol: " GADGET_SUBPROTOCOL "\r\n\r\n",
                               host, (unsigned)port, W.key);
  W.req_sent = W.resp_len = W.extra_len = W.extra_pos = 0;
  W.fd = fd;
  W.st = WS_CONNECTING;
  W.deadline = hal_now_ms() + CONNECT_TIMEOUT_MS;
  return GADGET_OK;
}

static gadget_status_t queue_msg(uint8_t opcode, const uint8_t *data, size_t len) {
  if (W.st != WS_OPEN) return GADGET_ERR_BUSY;
  struct wslay_event_msg m = {opcode, data, len};
  if (wslay_event_queue_msg(W.ctx, &m) != 0) return GADGET_ERR_IO;
  wslay_event_send(W.ctx);
  return GADGET_OK;
}

gadget_status_t hal_ws_send_text(const char *data, size_t len) {
  if (sim_net_scripted()) return sim_net_script_ws_send();
  return queue_msg(WSLAY_TEXT_FRAME, (const uint8_t *)data, len);
}

gadget_status_t hal_ws_send_binary(const uint8_t *data, size_t len) {
  if (sim_net_scripted()) return sim_net_script_ws_send();
  return queue_msg(WSLAY_BINARY_FRAME, data, len);
}

void hal_ws_close(uint16_t code) {
  if (sim_net_scripted()) {
    sim_net_script_ws_close(code);
    return;
  }
  if (W.st == WS_OPEN) {
    wslay_event_queue_close(W.ctx, code, NULL, 0);
    wslay_event_send(W.ctx);
    W.st = WS_CLOSING;
    W.close_code = code;
    W.deadline = hal_now_ms() + CLOSE_TIMEOUT_MS;
  } else if (W.st == WS_CONNECTING || W.st == WS_HANDSHAKE) {
    finish(0);
  }
}

void sim_net_poll(uint32_t wait_ms) {
  if (sim_net_scripted() || W.st == WS_IDLE) {
    if (wait_ms) poll(NULL, 0, (int)wait_ms);
    return;
  }
  if ((W.st == WS_CONNECTING || W.st == WS_HANDSHAKE || W.st == WS_CLOSING) && hal_now_ms() >= W.deadline) {
    hal_log(GADGET_LOG_WARN, TAG, W.st == WS_CLOSING ? "close timed out" : "connect timed out");
    finish(W.st == WS_CLOSING ? W.close_code : 0);
    return;
  }
  struct pollfd p = {.fd = W.fd, .events = 0};
  if (W.st == WS_CONNECTING) p.events = POLLOUT;
  else if (W.st == WS_HANDSHAKE) p.events = (short)(POLLIN | (W.req_sent < W.req_len ? POLLOUT : 0));
  else p.events = (short)((wslay_event_want_read(W.ctx) ? POLLIN : 0) | (wslay_event_want_write(W.ctx) ? POLLOUT : 0));
  bool buffered = W.extra_pos < W.extra_len;
  if (poll(&p, 1, buffered ? 0 : (int)wait_ms) < 0 && errno != EINTR) {
    finish(0);
    return;
  }
  if (buffered) p.revents |= POLLIN;
  if (W.st == WS_CONNECTING) {
    if (!(p.revents & (POLLOUT | POLLERR | POLLHUP))) return;
    int err = 0;
    socklen_t elen = sizeof err;
    getsockopt(W.fd, SOL_SOCKET, SO_ERROR, &err, &elen);
    if (err != 0) {
      hal_log(GADGET_LOG_WARN, TAG, "connect failed: %s", strerror(err));
      finish(0);
      return;
    }
    W.st = WS_HANDSHAKE;
    handshake_io(POLLOUT);
    return;
  }
  if (W.st == WS_HANDSHAKE) {
    handshake_io(p.revents);
    return;
  }
  if ((p.revents & (POLLIN | POLLHUP | POLLERR)) && wslay_event_recv(W.ctx) != 0) {
    finish(W.close_code);
    return;
  }
  if ((p.revents & POLLOUT) && wslay_event_send(W.ctx) != 0) {
    finish(W.close_code);
    return;
  }
  if (!wslay_event_want_read(W.ctx) && !wslay_event_want_write(W.ctx)) finish(W.close_code); /* closed both ways */
}
