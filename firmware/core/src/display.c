/* firmware/core/src/display.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* What the host puts on the screen (spec §4.5-§4.7, §5.4): asks and answers,
 * cards, images, post toasts with a chime, and battery `sense`. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_internal.h"
#include "gadget_ui.h"

#define TAG "display"
#define ASK_QUEUE 8
#define TOAST_MS 8000u
#define BATTERY_POLL_MS 1000u

typedef struct {
  char id[UI_ID_MAX];
  bool question;
  char title[UI_TITLE_MAX];
  char body[UI_BODY_MAX];
  uint8_t n;
  struct {
    char id[UI_ID_MAX];
    char label[UI_LABEL_MAX];
    ui_option_style_t style;
  } opt[UI_ASK_OPTIONS_MAX];
  uint32_t expires_s;
} ask_t;

static struct {
  ask_t q[ASK_QUEUE]; /* q[0] is on screen */
  size_t n;
  uint64_t expires_at;
  /* image being received */
  bool img_rx;
  uint8_t img_stream;
  char img_id[UI_ID_MAX];
  uint16_t img_w, img_h;
  uint32_t img_ttl_s;
  uint8_t *img_buf;
  size_t img_got;
  uint8_t *img_shown; /* published pixels, owned here */
  /* battery and sense */
  uint64_t batt_polled;
  bool batt_ok;
  gadget_battery_t batt;
  bool sense_base_ok;
  gadget_battery_t sense_base;
  uint64_t sense_at;
} D;

static bool touch_board(void) { return (g_core.board->input_mask & GADGET_INPUT_TOUCH) != 0; }

/* ---- asks ------------------------------------------------------------------------ */

static void show_head(void) {
  ui_model_t *m = &g_core.model;
  memset(&m->ask, 0, sizeof m->ask);
  m->ask.chosen = -1;
  g_core.f.ask_visible = D.n > 0;
  if (D.n == 0) return;
  const ask_t *a = &D.q[0];
  snprintf(m->ask.id, sizeof m->ask.id, "%s", a->id);
  m->ask.question = a->question;
  snprintf(m->ask.title, sizeof m->ask.title, "%s", a->title);
  snprintf(m->ask.body, sizeof m->ask.body, "%s", a->body);
  m->ask.n_options = a->n;
  gadget_rect_t rects[UI_ASK_OPTIONS_MAX] = {{0}};
  if (touch_board()) ui_layout_ask(g_core.board, a->n, rects);
  for (uint8_t i = 0; i < a->n; i++) {
    snprintf(m->ask.options[i].id, sizeof m->ask.options[i].id, "%s", a->opt[i].id);
    snprintf(m->ask.options[i].label, sizeof m->ask.options[i].label, "%s", a->opt[i].label);
    m->ask.options[i].style = a->opt[i].style;
    m->ask.options[i].rect = rects[i];
  }
  m->ask.answerable = a->n >= 1 && a->n <= (touch_board() ? UI_ASK_OPTIONS_MAX : 2);
  m->ask.locked_until_ms = g_core.now + GADGET_ASK_LOCK_MS;
  m->ask.queued = (uint8_t)(D.n - 1);
  D.expires_at = a->expires_s ? g_core.now + (uint64_t)a->expires_s * 1000u : 0;
}

static void remove_ask(size_t i) {
  if (i >= D.n) return;
  memmove(&D.q[i], &D.q[i + 1], (D.n - i - 1) * sizeof D.q[0]);
  D.n--;
  if (i == 0) {
    show_head();
  } else {
    g_core.model.ask.queued = (uint8_t)(D.n - 1);
  }
}

static void on_ask(const gp_ask_t *a) {
  size_t i = 0;
  while (i < D.n && strcmp(D.q[i].id, a->id) != 0) i++;
  if (i == D.n) {
    if (D.n == ASK_QUEUE) {
      hal_log(GADGET_LOG_WARN, TAG, "ask queue full; dropping %s", a->id);
      return;
    }
    D.n++;
  }
  ask_t *q = &D.q[i];
  memset(q, 0, sizeof *q);
  snprintf(q->id, sizeof q->id, "%s", a->id);
  q->question = a->kind == GP_ASK_QUESTION;
  core_text_copy(q->title, sizeof q->title, a->title);
  core_text_copy(q->body, sizeof q->body, a->body ? a->body : "");
  q->n = a->n_options;
  for (uint8_t k = 0; k < a->n_options; k++) {
    snprintf(q->opt[k].id, sizeof q->opt[k].id, "%s", a->options[k].id);
    core_text_copy(q->opt[k].label, sizeof q->opt[k].label, a->options[k].label);
    q->opt[k].style = a->options[k].style == GP_STYLE_ALLOW  ? UI_STYLE_ALLOW
                      : a->options[k].style == GP_STYLE_DENY ? UI_STYLE_DENY
                                                             : UI_STYLE_NEUTRAL;
  }
  q->expires_s = a->expires_s;
  if (i == 0) {
    show_head();
  } else {
    g_core.model.ask.queued = (uint8_t)(D.n - 1);
  }
}

static void answer(int idx) {
  ui_model_t *m = &g_core.model;
  if (idx < 0 || idx >= m->ask.n_options || m->ask.chosen >= 0 || !m->ask.answerable) return;
  if (g_core.now < m->ask.locked_until_ms) return; /* presses in the first 0.6 s are ignored */
  gp_answer_t ans = {.id = m->ask.id, .option = m->ask.options[idx].id};
  if (session_send("answer", g_core_tx, gp_encode_answer(g_core_tx, sizeof g_core_tx, &ans)) == GADGET_OK) {
    m->ask.chosen = (int8_t)idx;
  }
}

bool display_ask_input(const gadget_input_t *in) {
  /* only the ask on screen: never one hidden behind Listening or Update */
  if (!g_core.f.ask_visible || g_core.model.screen != UI_SCREEN_ASK) return false;
  if (touch_board()) {
    switch (in->type) {
      case GADGET_IN_TOUCH_DOWN:
      case GADGET_IN_TOUCH_MOVE:
        return true;
      case GADGET_IN_TOUCH_UP:
        answer(ui_hit_test(&g_core.model, in->x, in->y));
        return true;
      default:
        return false; /* TALK and CANCEL keep their meaning on touch boards */
    }
  }
  const ui_model_t *m = &g_core.model;
  if (!m->ask.answerable) return false;
  if (in->type == GADGET_IN_TALK_DOWN) {
    answer(0);
    return true;
  }
  if (in->type == GADGET_IN_TALK_UP) return true;
  if (m->ask.n_options == 2 && (in->type == GADGET_IN_CANCEL_DOWN || in->type == GADGET_IN_CANCEL_UP)) {
    if (in->type == GADGET_IN_CANCEL_DOWN) answer(1);
    return true;
  }
  return false;
}

/* ---- cards and images ------------------------------------------------------------ */

static void hide_card(void) {
  memset(&g_core.model.card, 0, sizeof g_core.model.card);
  g_core.f.card_visible = false;
}

static void hide_image(void) {
  ui_model_t *m = &g_core.model;
  free(D.img_shown);
  D.img_shown = NULL;
  uint32_t rev = m->image.pixels_rev;
  memset(&m->image, 0, sizeof m->image);
  m->image.pixels_rev = rev + 1;
  g_core.f.image_visible = false;
}

static void on_card(const gp_card_t *c) {
  ui_model_t *m = &g_core.model;
  snprintf(m->card.id, sizeof m->card.id, "%s", c->id);
  core_text_copy(m->card.title, sizeof m->card.title, c->title);
  core_text_copy(m->card.body, sizeof m->card.body, c->body ? c->body : "");
  m->card.expires_ms = c->ttl_s ? g_core.now + (uint64_t)c->ttl_s * 1000u : 0;
  g_core.f.card_visible = true;
}

static void image_rx_reset(void) {
  free(D.img_buf);
  D.img_buf = NULL;
  D.img_rx = false;
  D.img_got = 0;
}

static void on_image_begin(const gp_image_begin_t *b) {
  image_rx_reset();
  if (b->w > g_core.board->image_w || b->h > g_core.board->image_h) {
    hal_log(GADGET_LOG_WARN, TAG, "image %ux%u is larger than caps.image", (unsigned)b->w, (unsigned)b->h);
    return;
  }
  D.img_buf = malloc((size_t)b->w * b->h * 2u);
  if (D.img_buf == NULL) return;
  D.img_rx = true;
  D.img_stream = b->stream;
  snprintf(D.img_id, sizeof D.img_id, "%s", b->id);
  D.img_w = b->w;
  D.img_h = b->h;
  D.img_ttl_s = b->ttl_s;
}

static void on_image_rows(uint8_t stream, const uint8_t *payload, size_t len) {
  if (!D.img_rx || stream != D.img_stream) return;
  size_t total = (size_t)D.img_w * D.img_h * 2u;
  size_t n = len > total - D.img_got ? total - D.img_got : len;
  memcpy(D.img_buf + D.img_got, payload, n);
  D.img_got += n;
}

static void on_image_end(uint8_t stream) {
  if (!D.img_rx || stream != D.img_stream) return;
  if (D.img_got != (size_t)D.img_w * D.img_h * 2u) {
    hal_log(GADGET_LOG_WARN, TAG, "image %s incomplete; dropped", D.img_id);
    image_rx_reset();
    return;
  }
  hide_image();
  ui_model_t *m = &g_core.model;
  D.img_shown = D.img_buf;
  D.img_buf = NULL;
  D.img_rx = false;
  snprintf(m->image.id, sizeof m->image.id, "%s", D.img_id);
  m->image.w = D.img_w;
  m->image.h = D.img_h;
  m->image.pixels = (const uint16_t *)(const void *)D.img_shown;
  m->image.pixels_rev++;
  m->image.expires_ms = D.img_ttl_s ? g_core.now + (uint64_t)D.img_ttl_s * 1000u : 0;
  g_core.f.image_visible = true;
}

/* ---- posts ------------------------------------------------------------------------- */

static void on_post(const gp_post_t *p) {
  ui_model_t *m = &g_core.model;
  m->toast.visible = true;
  m->toast.kind = p->kind == GP_POST_ROUTINE ? UI_POST_ROUTINE : UI_POST_MESSAGE;
  core_text_copy(m->toast.bot_name, sizeof m->toast.bot_name, p->bot.name);
  core_text_copy(m->toast.text, sizeof m->toast.text, p->text);
  m->toast.until_ms = g_core.now + TOAST_MS;
  g_core.f.toast_visible = true;
  audio_play_chime();
}

static void hide_toast(void) {
  g_core.model.toast.visible = false;
  g_core.f.toast_visible = false;
}

/* ---- module API ---------------------------------------------------------------------- */

bool display_on_msg(const gp_msg_t *m) {
  switch (m->op) {
    case GP_OP_ASK:
      on_ask(&m->m.ask);
      break;
    case GP_OP_ASK_CLOSE:
      for (size_t i = 0; i < D.n; i++) {
        if (strcmp(D.q[i].id, m->m.ask_close.id) == 0) {
          remove_ask(i);
          break;
        }
      }
      break;
    case GP_OP_POST:
      on_post(&m->m.post);
      break;
    case GP_OP_CARD:
      on_card(&m->m.card);
      break;
    case GP_OP_CARD_CLOSE:
      if (g_core.f.card_visible && strcmp(g_core.model.card.id, m->m.card_close.id) == 0) hide_card();
      if (g_core.f.image_visible && strcmp(g_core.model.image.id, m->m.card_close.id) == 0) hide_image();
      break;
    case GP_OP_IMAGE_BEGIN:
      on_image_begin(&m->m.image_begin);
      break;
    case GP_OP_IMAGE_END:
      on_image_end(m->m.image_end.stream);
      break;
    default:
      return false;
  }
  return true;
}

void display_on_binary(uint8_t stream, const uint8_t *payload, size_t len) { on_image_rows(stream, payload, len); }

bool display_dismiss(void) {
  if (g_core.f.image_visible) {
    hide_image();
    return true;
  }
  if (g_core.f.card_visible) {
    hide_card();
    return true;
  }
  if (g_core.f.toast_visible) {
    hide_toast();
    return true;
  }
  return false;
}

bool display_tap(void) {
  if (!g_core.f.toast_visible) return false;
  hide_toast();
  return true;
}

static void battery_tick(void) {
  uint64_t now = g_core.now;
  ui_model_t *m = &g_core.model;
  if (!g_core.board->has_battery) return;
  if (D.batt_polled != 0 && now - D.batt_polled < BATTERY_POLL_MS) return;
  D.batt_polled = now;
  gadget_battery_t b;
  D.batt_ok = hal_battery_read(&b);
  if (D.batt_ok) {
    if (b.pct > 100) b.pct = 100;
    D.batt = b;
  }
  m->battery.present = D.batt_ok;
  m->battery.pct = D.batt_ok ? D.batt.pct : 0;
  m->battery.charging = D.batt_ok && D.batt.charging;
  if (!session_ready() || !D.batt_ok) return;
  if (!D.sense_base_ok) { /* hello carried this reading */
    D.sense_base = D.batt;
    D.sense_base_ok = true;
    D.sense_at = now;
    return;
  }
  bool changed = D.batt.pct != D.sense_base.pct || D.batt.charging != D.sense_base.charging;
  if (changed && now - D.sense_at >= GADGET_SENSE_MIN_INTERVAL_MS) {
    gp_sense_t s = {.battery_valid = true, .battery_pct = D.batt.pct, .charging = D.batt.charging};
    if (session_send("sense", g_core_tx, gp_encode_sense(g_core_tx, sizeof g_core_tx, &s)) == GADGET_OK) {
      D.sense_base = D.batt;
      D.sense_at = now;
    }
  }
}

void display_tick(void) {
  uint64_t now = g_core.now;
  ui_model_t *m = &g_core.model;
  if (D.n > 0 && D.expires_at != 0 && now >= D.expires_at) remove_ask(0);
  if (g_core.f.card_visible && m->card.expires_ms != 0 && now >= m->card.expires_ms) hide_card();
  if (g_core.f.image_visible && m->image.expires_ms != 0 && now >= m->image.expires_ms) hide_image();
  if (g_core.f.toast_visible && now >= m->toast.until_ms) hide_toast();
  battery_tick();
}

void display_on_session_lost(void) {
  D.n = 0; /* open asks are sent again on reconnect */
  show_head();
  image_rx_reset();
  D.sense_base_ok = false;
}

void display_init(void) { memset(&D, 0, sizeof D); }

void display_deinit(void) {
  image_rx_reset();
  free(D.img_shown);
  memset(&D, 0, sizeof D);
}
