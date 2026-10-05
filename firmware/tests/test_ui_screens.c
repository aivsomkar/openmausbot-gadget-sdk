/* SPDX-License-Identifier: Apache-2.0 */
/* Every screen on every board size, drawn by the real UI on LVGL's test
 * display: the copy that must show, what must be hidden, and that every
 * visible text box stays on the screen (inside the circle on round boards). */
#include <stdio.h>
#include <string.h>

#include "gadget_board.h"
#include "gadget_ui.h"
#include "lvgl.h"
#include "ui_copy.h"
#include "ui_lv_compat.h"
#include "ui_pager.h"
#include "ui_priv.h" /* g_ui.applies: the render-cache test */
#include "art/maus_art.h"
#include "unity.h"

static ui_model_t m;
static const gadget_board_t *board;
static lv_display_t *disp;
static uint16_t k_pixels[40 * 30];

static const char *const k_boards[] = {"amoled-175c", "amoled-175", "lcd-154", "devkit"};

void setUp(void) {}

static bool g_started;

static void stop(void) {
  if (!g_started) return;
  ui_deinit();
  lv_deinit();
  g_started = false;
}

static void start(const char *id) {
  stop();
  board = gadget_board_by_id(id);
  TEST_ASSERT_NOT_NULL(board);
  lv_init();
  disp = lv_test_display_create(board->screen_w, board->screen_h);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  memset(&m, 0, sizeof m);
  strcpy(m.device_id, "gad_b18b86ce1389e46d");
  g_started = true;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, ui_init(board, 1));
}

void tearDown(void) { stop(); }

static void render(uint64_t now) {
  m.rev++;
  m.now_ms = now;
  lv_tick_inc(10);
  ui_render(&m);
  ui_tick(now);
  lv_obj_update_layout(lv_screen_active());
  lv_refr_now(disp);
}

static bool visible(const lv_obj_t *o) {
  for (; o; o = lv_obj_get_parent(o)) {
    if (ui_is_hidden(o)) return false;
  }
  return true;
}

static bool clip(lv_area_t *a, const lv_area_t *b) {
  a->x1 = LV_MAX(a->x1, b->x1);
  a->y1 = LV_MAX(a->y1, b->y1);
  a->x2 = LV_MIN(a->x2, b->x2);
  a->y2 = LV_MIN(a->y2, b->y2);
  return a->x1 <= a->x2 && a->y1 <= a->y2;
}

/* The part of `o` that can show: its area clipped by every ancestor. */
static bool shown_area(const lv_obj_t *o, lv_area_t *out) {
  lv_obj_get_coords(o, out);
  for (const lv_obj_t *p = lv_obj_get_parent(o); p; p = lv_obj_get_parent(p)) {
    lv_area_t pa;
    lv_obj_get_coords(p, &pa);
    if (!clip(out, &pa)) return false;
  }
  return true;
}

typedef struct {
  const char *needle;
  bool found;
  lv_obj_t *obj;
} find_t;

static void walk(lv_obj_t *o, void (*fn)(lv_obj_t *, void *), void *ctx) {
  fn(o, ctx);
  uint32_t n = lv_obj_get_child_count(o);
  for (uint32_t i = 0; i < n; i++) walk(lv_obj_get_child(o, (int32_t)i), fn, ctx);
}

static void find_cb(lv_obj_t *o, void *ctx) {
  find_t *f = ctx;
  if (lv_obj_check_type(o, &lv_label_class) && visible(o) && strstr(lv_label_get_text(o), f->needle)) {
    f->found = true;
    f->obj = o;
  }
}

static bool shows(const char *needle) {
  find_t f = {needle, false, NULL};
  walk(lv_screen_active(), find_cb, &f);
  return f.found;
}

static bool inside(int32_t x, int32_t y) {
  if (x < 0 || y < 0 || x >= board->screen_w || y >= board->screen_h) return false;
  if (!board->screen_round) return true;
  int32_t r = board->screen_w / 2;
  int32_t dx = x - r;
  int32_t dy = y - r;
  return dx * dx + dy * dy <= r * r;
}

static void bounds_cb(lv_obj_t *o, void *ctx) {
  (void)ctx;
  if (!lv_obj_check_type(o, &lv_label_class) || !visible(o) || lv_label_get_text(o)[0] == '\0') return;
  lv_area_t a;
  if (!shown_area(o, &a)) return;
  char msg[320];
  snprintf(msg, sizeof msg, "%s: text \"%.40s\" at %d,%d-%d,%d leaves the screen", board->id, lv_label_get_text(o),
           (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2);
  TEST_ASSERT_TRUE_MESSAGE(inside(a.x1, a.y1) && inside(a.x2, a.y1) && inside(a.x1, a.y2) && inside(a.x2, a.y2), msg);
}

static void assert_text_on_screen(void) { walk(lv_screen_active(), bounds_cb, NULL); }

/* The visible label whose text contains `needle`. */
static lv_obj_t *label_with(const char *needle) {
  find_t f = {needle, false, NULL};
  walk(lv_screen_active(), find_cb, &f);
  TEST_ASSERT_NOT_NULL_MESSAGE(f.obj, needle);
  return f.obj;
}

/* A label's y inside its parent, after LVGL has applied pending moves. */
static int32_t label_y(lv_obj_t *l) {
  lv_obj_update_layout(lv_screen_active());
  return lv_obj_get_y(l);
}

static uint16_t centre_pixel(void) {
  lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
  uint16_t px;
  memcpy(&px, buf->data + (uint32_t)(board->screen_h / 2) * buf->header.stride + (uint32_t)(board->screen_w / 2) * 2u, 2);
  return px;
}

static void each_board(void (*body)(void)) {
  for (size_t i = 0; i < sizeof k_boards / sizeof k_boards[0]; i++) {
    start(k_boards[i]);
    body();
    stop();
  }
}

/* ---- Maus screens ------------------------------------------------------ */

static void idle_body(void) {
  m.screen = UI_SCREEN_IDLE;
  m.maus = UI_MAUS_IDLE;
  render(1000);
  TEST_ASSERT_TRUE(shows("Hi"));
  strcpy(m.bot_name, "Jev");
  render(1010);
  TEST_ASSERT_TRUE(shows("Hi, I'm Jev"));
  assert_text_on_screen();
}
static void test_idle(void) { each_board(idle_body); }

static void setup_body(void) {
  m.screen = UI_SCREEN_SETUP;
  m.maus = UI_MAUS_CURIOUS;
  for (int step = UI_SETUP_NEED_WIFI; step <= UI_SETUP_DEVICE_LIMIT; step++) {
    m.setup.step = (ui_setup_step_t)step;
    for (uint64_t t = 0; t <= 6 * UI_PAGE_ROTATE_MS; t += UI_PAGE_ROTATE_MS) { /* every rotated page */
      render(t);
      assert_text_on_screen();
    }
    ui_copy_t c;
    ui_copy_setup(&m, &c);
    TEST_ASSERT_TRUE(shows(c.caption));
    TEST_ASSERT_TRUE(shows("gad_b18b86ce1389e46d"));
  }
  m.setup.step = UI_SETUP_NEED_CODE;
  render(0);
  TEST_ASSERT_TRUE(shows("Pair me: MausBot " UI_ARROW " Settings " UI_ARROW " Remote access " UI_ARROW " Pair a gadget"));
  TEST_ASSERT_TRUE(shows("Remote access must be on."));
  TEST_ASSERT_FALSE(shows("Omkar's computer"));
  strcpy(m.host_name, "Omkar's computer"); /* a challenge has arrived (spec 5.5) */
  m.setup.step = UI_SETUP_BAD_CODE;
  render(0);
  TEST_ASSERT_TRUE(shows("Omkar's computer"));
  TEST_ASSERT_TRUE(shows("gad_b18b86ce1389e46d"));
  assert_text_on_screen();
}
static void test_setup(void) { each_board(setup_body); }

static void offline_body(void) {
  m.screen = UI_SCREEN_OFFLINE;
  m.maus = UI_MAUS_SLEEPING;
  strcpy(m.host_name, "Omkar's computer");
  for (int r = UI_OFFLINE_WIFI_CONNECTING; r <= UI_OFFLINE_PROTOCOL; r++) {
    m.offline.reason = (ui_offline_reason_t)r;
    m.offline.retry_at_ms = (r == UI_OFFLINE_HOST_UNREACHABLE || r == UI_OFFLINE_PROTOCOL) ? 9000 : 0;
    render(1000);
    assert_text_on_screen();
    render(5000);
    assert_text_on_screen();
  }
  m.offline.reason = UI_OFFLINE_HOST_UNREACHABLE;
  m.offline.retry_at_ms = 9000;
  render(1000);
  TEST_ASSERT_TRUE(shows("Can't reach Omkar's computer."));
  TEST_ASSERT_TRUE(shows("Retrying in 8 s"));
  /* the countdown moves without a model change */
  m.now_ms = 6500;
  ui_render(&m);
  TEST_ASSERT_TRUE(shows("Retrying in 3 s"));
  /* the stored host_name on its own line where the caption does not name it (spec 5.5) */
  m.offline.reason = UI_OFFLINE_IN_USE_ELSEWHERE;
  m.offline.retry_at_ms = 0;
  render(7000);
  TEST_ASSERT_TRUE(shows("Omkar's computer"));
  m.offline.reason = UI_OFFLINE_PROTOCOL;
  m.offline.retry_at_ms = 9000;
  render(7010);
  TEST_ASSERT_TRUE(shows("Omkar's computer"));
  TEST_ASSERT_TRUE(shows("Retrying in 2 s"));
  assert_text_on_screen();
}
static void test_offline(void) { each_board(offline_body); }

static void listening_body(void) {
  m.screen = UI_SCREEN_LISTENING;
  m.maus = UI_MAUS_LISTENING;
  m.mic_level = 200;
  render(100);
  TEST_ASSERT_FALSE(shows("5"));
  m.listening.countdown_s = 3;
  render(56000);
  TEST_ASSERT_TRUE(shows("3"));
  assert_text_on_screen();
}
static void test_listening(void) { each_board(listening_body); }

static void thinking_body(void) {
  m.screen = UI_SCREEN_THINKING;
  m.maus = UI_MAUS_THINKING;
  strcpy(m.thinking.heard, "What's on my calendar today?");
  render(100);
  TEST_ASSERT_TRUE(shows("What's on my calendar today?"));
  m.maus = UI_MAUS_WORKING;
  strcpy(m.thinking.working, "checking your calendar");
  render(200);
  TEST_ASSERT_TRUE(shows("checking your calendar"));
  assert_text_on_screen();
}
static void test_thinking(void) { each_board(thinking_body); }

static void reply_body(void) {
  m.screen = UI_SCREEN_REPLY;
  m.maus = UI_MAUS_IDLE;
  strcpy(m.reply.text, "You have two meetings today: design review at 10 and lunch with Sam at 1.");
  m.reply.final = true;
  render(100);
  TEST_ASSERT_TRUE(shows("lunch with Sam at 1."));
  assert_text_on_screen();
  m.maus = UI_MAUS_ALERTING;
  m.reply.failed = true;
  strcpy(m.reply.reason, "Connection lost");
  render(200);
  TEST_ASSERT_TRUE(shows("Connection lost"));
  TEST_ASSERT_FALSE(shows("lunch with Sam"));
  assert_text_on_screen();
}
static void test_reply(void) { each_board(reply_body); }

static void speaking_body(void) {
  m.screen = UI_SCREEN_SPEAKING;
  m.maus = UI_MAUS_SPEAKING;
  m.speak_level = 2;
  for (size_t i = 0; i < 40; i++) strcat(m.reply.text, "Word after word. ");
  render(100);
  assert_text_on_screen();
  m.reply.speak_total_ms = 20000;
  m.reply.speak_elapsed_ms = 19000;
  render(200);
  assert_text_on_screen();
}
static void test_speaking(void) { each_board(speaking_body); }

/* ---- text screens ------------------------------------------------------ */

static void fill_ask(uint8_t n, bool question) {
  static const char *const labels[] = {"Allow", "Deny", "Maybe", "Later"};
  m.screen = UI_SCREEN_ASK;
  m.maus = UI_MAUS_NONE;
  strcpy(m.ask.id, "ask-1");
  m.ask.question = question;
  strcpy(m.ask.title, question ? "Which room?" : "Run shell command?");
  strcpy(m.ask.body, "ls -la ~/Documents and then summarise what is in there for me");
  m.ask.n_options = n;
  gadget_rect_t rects[UI_ASK_OPTIONS_MAX] = {{0}};
  ui_layout_ask(board, n, rects);
  for (uint8_t i = 0; i < n; i++) {
    strcpy(m.ask.options[i].id, labels[i]);
    strcpy(m.ask.options[i].label, labels[i]);
    m.ask.options[i].style = question ? UI_STYLE_NEUTRAL : (i == 0 ? UI_STYLE_ALLOW : UI_STYLE_DENY);
    m.ask.options[i].rect = rects[i];
  }
  bool touch = (board->input_mask & GADGET_INPUT_TOUCH) != 0;
  m.ask.answerable = n > 0 && (touch ? n <= 4 : n <= 2);
  m.ask.chosen = -1;
  m.ask.locked_until_ms = 600;
}

static void ask_body(void) {
  fill_ask(2, false);
  render(0);
  TEST_ASSERT_TRUE(shows("Run shell command?"));
  TEST_ASSERT_TRUE(shows("Allow"));
  TEST_ASSERT_TRUE(shows("Deny"));
  TEST_ASSERT_FALSE(shows(UI_COPY_ASK_ELSEWHERE));
  assert_text_on_screen();
  render(1000);

  fill_ask(3, true);
  render(2000);
  if (board->input_mask & GADGET_INPUT_TOUCH) {
    TEST_ASSERT_TRUE(shows("Maybe"));
  } else {
    TEST_ASSERT_TRUE(shows(UI_COPY_ASK_ELSEWHERE));
    TEST_ASSERT_FALSE(shows("Maybe"));
  }
  assert_text_on_screen();

  fill_ask(0, true);
  render(3000);
  TEST_ASSERT_TRUE(shows(UI_COPY_ASK_ELSEWHERE));
  TEST_ASSERT_TRUE(shows("Which room?"));
  assert_text_on_screen();
}
static void test_ask(void) { each_board(ask_body); }

static void card_body(void) {
  m.screen = UI_SCREEN_CARD;
  m.maus = UI_MAUS_NONE;
  strcpy(m.card.title, "Build finished");
  for (int i = 0; i < 30; i++) strcat(m.card.body, "All 312 tests passed. ");
  render(100);
  TEST_ASSERT_TRUE(shows("Build finished"));
  assert_text_on_screen();
}
static void test_card(void) { each_board(card_body); }

static void update_body(void) {
  m.screen = UI_SCREEN_UPDATE;
  m.maus = UI_MAUS_NONE;
  m.update.phase = UI_UPDATE_RECEIVING;
  m.update.pct = 42;
  render(100);
  TEST_ASSERT_TRUE(shows("Updating" UI_ELLIPSIS " 42%"));
  m.update.phase = UI_UPDATE_VERIFYING;
  render(200);
  TEST_ASSERT_TRUE(shows("Checking update" UI_ELLIPSIS));
  m.update.phase = UI_UPDATE_RESTARTING;
  render(300);
  TEST_ASSERT_TRUE(shows("Restarting" UI_ELLIPSIS));
  assert_text_on_screen();
}
static void test_update(void) { each_board(update_body); }

static void image_body(void) {
  for (size_t i = 0; i < sizeof k_pixels / sizeof k_pixels[0]; i++) k_pixels[i] = 0xF800; /* red */
  m.screen = UI_SCREEN_IMAGE;
  m.maus = UI_MAUS_NONE;
  m.image.w = 40;
  m.image.h = 30;
  m.image.pixels = k_pixels;
  m.image.pixels_rev = 1;
  render(100);
  TEST_ASSERT_EQUAL_HEX16(0xF800, centre_pixel());
  /* new pixels under a new pixels_rev show up */
  for (size_t i = 0; i < sizeof k_pixels / sizeof k_pixels[0]; i++) k_pixels[i] = 0x001F; /* blue */
  m.image.pixels_rev = 2;
  render(200);
  TEST_ASSERT_EQUAL_HEX16(0x001F, centre_pixel());
  /* a third picture reuses the first descriptor slot with another size */
  static uint16_t small[20 * 10];
  for (size_t i = 0; i < sizeof small / sizeof small[0]; i++) small[i] = 0x07E0; /* green */
  m.image.w = 20;
  m.image.h = 10;
  m.image.pixels = small;
  m.image.pixels_rev = 3;
  render(300);
  TEST_ASSERT_EQUAL_HEX16(0x07E0, centre_pixel());
}
static void test_image(void) { each_board(image_body); }

static void toast_body(void) {
  m.screen = UI_SCREEN_IDLE;
  m.maus = UI_MAUS_NOTIFYING;
  m.toast.visible = true;
  m.toast.kind = UI_POST_ROUTINE;
  strcpy(m.toast.bot_name, "Jev");
  strcpy(m.toast.text, "Morning brief is ready: 3 meetings, 2 reviews waiting.");
  render(100);
  TEST_ASSERT_TRUE(shows("Morning brief is ready"));
  assert_text_on_screen();
  m.toast.visible = false;
  render(200);
  TEST_ASSERT_FALSE(shows("Morning brief is ready"));
}
static void test_toast(void) { each_board(toast_body); }

/* A toast sits over any screen (spec 5.5). On the smaller boards it overlaps
 * the caption or the Maus, and nothing under it may show through: an empty
 * toast over "Hi, I'm Jev" draws one flat colour inside its rounded corners. */
static void toast_cover_body(void) {
  m.screen = UI_SCREEN_IDLE;
  m.maus = UI_MAUS_NOTIFYING;
  strcpy(m.bot_name, "Jev");
  m.toast.visible = true; /* no bot name, no text: only the toast's own background */
  render(100);
  lv_area_t a;
  lv_obj_get_coords(g_ui.toast, &a);
  const int32_t r = lv_obj_get_style_radius(g_ui.toast, 0);
  lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
  uint16_t first = 0;
  bool have = false;
  for (int32_t y = a.y1 + r; y <= a.y2 - r; y++) {
    for (int32_t x = a.x1 + r; x <= a.x2 - r; x++) {
      uint16_t px;
      memcpy(&px, buf->data + (uint32_t)y * buf->header.stride + (uint32_t)x * 2u, 2);
      if (!have) {
        first = px;
        have = true;
      }
      char msg[96];
      snprintf(msg, sizeof msg, "%s: something under the toast shows through at %d,%d", board->id, (int)x, (int)y);
      TEST_ASSERT_EQUAL_HEX16_MESSAGE(first, px, msg);
    }
  }
  TEST_ASSERT_TRUE(have);
}
static void test_toast_covers_what_is_under_it(void) { each_board(toast_cover_body); }

/* ...and no line of text is left half under it: the caption either clears
 * the toast or steps aside while the toast shows. */
static lv_area_t g_toast_area;

static bool is_in_toast(const lv_obj_t *o) {
  for (; o; o = lv_obj_get_parent(o)) {
    if (o == g_ui.toast) return true;
  }
  return false;
}

static void toast_overlap_cb(lv_obj_t *o, void *ctx) {
  (void)ctx;
  if (!lv_obj_check_type(o, &lv_label_class) || !visible(o) || is_in_toast(o) || lv_label_get_text(o)[0] == '\0') return;
  lv_area_t a;
  if (!shown_area(o, &a)) return;
  char msg[160];
  snprintf(msg, sizeof msg, "%s: \"%.40s\" is cut by the toast", board->id, lv_label_get_text(o));
  TEST_ASSERT_FALSE_MESSAGE(clip(&a, &g_toast_area), msg);
}

static void toast_no_cut_body(void) {
  m.maus = UI_MAUS_NOTIFYING;
  m.toast.visible = true;
  strcpy(m.toast.bot_name, "Jev");
  strcpy(m.toast.text, "Morning brief is ready: 3 meetings, 2 reviews waiting.");
  strcpy(m.bot_name, "Jev");
  strcpy(m.host_name, "Omkar's computer");
  m.screen = UI_SCREEN_IDLE;
  render(100);
  lv_obj_get_coords(g_ui.toast, &g_toast_area);
  walk(lv_screen_active(), toast_overlap_cb, NULL);
  m.screen = UI_SCREEN_OFFLINE;
  m.maus = UI_MAUS_SLEEPING;
  m.offline.reason = UI_OFFLINE_PROTOCOL;
  m.offline.retry_at_ms = 9000;
  render(200);
  walk(lv_screen_active(), toast_overlap_cb, NULL);
  TEST_ASSERT_TRUE(shows("Morning brief is ready"));
  m.toast.visible = false; /* the caption comes back with the toast gone */
  render(300);
  TEST_ASSERT_TRUE(shows("MausBot didn't accept me."));
}
static void test_toast_never_cuts_text(void) { each_board(toast_no_cut_body); }

/* ---- Review Focus ------------------------------------------------------- */

static void long_text_body(void) {
  memset(m.bot_name, 'W', sizeof m.bot_name - 1); /* one 95-character word */
  m.bot_name[sizeof m.bot_name - 1] = '\0';
  m.screen = UI_SCREEN_IDLE;
  m.maus = UI_MAUS_IDLE;
  render(100);
  assert_text_on_screen();
  m.screen = UI_SCREEN_REPLY;
  memset(m.reply.text, 'a', UI_REPLY_MAX - 1); /* 8 KiB without a space */
  m.reply.text[UI_REPLY_MAX - 1] = '\0';
  render(200);
  assert_text_on_screen();
  fill_ask(2, false);
  memset(m.ask.title, 'T', UI_TITLE_MAX - 1);
  m.ask.title[UI_TITLE_MAX - 1] = '\0';
  memset(m.ask.body, 'b', UI_BODY_MAX - 1);
  m.ask.body[UI_BODY_MAX - 1] = '\0';
  render(300);
  assert_text_on_screen();
  /* a 95-character host name with no space, on Setup and on Offline */
  memset(m.host_name, 'H', sizeof m.host_name - 1);
  m.host_name[sizeof m.host_name - 1] = '\0';
  m.screen = UI_SCREEN_SETUP;
  m.maus = UI_MAUS_CURIOUS;
  m.setup.step = UI_SETUP_PAIRING;
  render(400);
  assert_text_on_screen();
  m.setup.step = UI_SETUP_BAD_CODE; /* the host name on its own line */
  render(500);
  assert_text_on_screen();
  m.screen = UI_SCREEN_OFFLINE;
  m.maus = UI_MAUS_SLEEPING;
  m.offline.reason = UI_OFFLINE_HOST_UNREACHABLE;
  m.offline.retry_at_ms = 9000;
  render(600);
  assert_text_on_screen();
  m.offline.reason = UI_OFFLINE_PROTOCOL;
  render(700);
  assert_text_on_screen();
  /* a toast with a 95-character bot name and 511 characters, both unbroken */
  m.screen = UI_SCREEN_IDLE;
  m.maus = UI_MAUS_NOTIFYING;
  m.toast.visible = true;
  memset(m.toast.bot_name, 'N', sizeof m.toast.bot_name - 1);
  m.toast.bot_name[sizeof m.toast.bot_name - 1] = '\0';
  memset(m.toast.text, 't', UI_TOAST_MAX - 1);
  m.toast.text[UI_TOAST_MAX - 1] = '\0';
  render(800);
  assert_text_on_screen();
  m.toast.visible = false;
  /* a card with a 191-byte title and a 1535-byte body, both unbroken */
  m.screen = UI_SCREEN_CARD;
  m.maus = UI_MAUS_NONE;
  memset(m.card.title, 'C', UI_TITLE_MAX - 1);
  m.card.title[UI_TITLE_MAX - 1] = '\0';
  memset(m.card.body, 'c', UI_BODY_MAX - 1);
  m.card.body[UI_BODY_MAX - 1] = '\0';
  render(900);
  assert_text_on_screen();
}
static void test_long_unbroken_text_stays_on_screen(void) { each_board(long_text_body); }

static void bad_enum_body(void) {
  m.screen = (ui_screen_t)99;
  m.maus = (ui_maus_state_t)99;
  render(100);
  m.screen = UI_SCREEN_OFFLINE;
  m.offline.reason = (ui_offline_reason_t)99;
  m.maus = UI_MAUS_SLEEPING;
  render(200);
  assert_text_on_screen();
}
static void test_out_of_range_model_values_do_not_crash(void) { each_board(bad_enum_body); }

static void ask_unlock_body(void) {
  fill_ask(2, false); /* locked until 600 ms */
  render(0);
  find_t f = {"Allow", false, NULL};
  walk(lv_screen_active(), find_cb, &f);
  TEST_ASSERT_NOT_NULL(f.obj);
  lv_obj_t *button = lv_obj_get_parent(f.obj);
  TEST_ASSERT_EQUAL_UINT8(LV_OPA_50, lv_obj_get_style_opa(button, 0));
  m.now_ms = 700; /* time only: no model change */
  ui_render(&m);
  TEST_ASSERT_EQUAL_UINT8(LV_OPA_COVER, lv_obj_get_style_opa(button, 0));
}
static void test_ask_unlocks_without_a_model_change(void) { each_board(ask_unlock_body); }

static void image_pending_body(void) {
  m.screen = UI_SCREEN_IMAGE;
  m.maus = UI_MAUS_NONE;
  m.image.w = 40;
  m.image.h = 30;
  m.image.pixels = NULL; /* image.begin seen, image.end not yet */
  render(100);
  TEST_ASSERT_EQUAL_HEX16(0x0000, centre_pixel());
}
static void test_image_without_pixels_draws_nothing(void) { each_board(image_pending_body); }

/* Review Focus 3: copy longer than its box turns its page with time alone. */
static void assert_page_turns(const char *needle) {
  render(0);
  lv_obj_t *l = label_with(needle);
  int32_t y0 = label_y(l);
  TEST_ASSERT_TRUE_MESSAGE(lv_obj_get_height(l) > lv_obj_get_height(lv_obj_get_parent(l)), "the copy fits on one page");
  m.now_ms = UI_PAGE_ROTATE_MS; /* time only: rev unchanged */
  ui_render(&m);
  TEST_ASSERT_NOT_EQUAL(y0, label_y(l));
}

static void test_pages_turn_without_a_model_change(void) {
  start("lcd-154");
  m.screen = UI_SCREEN_SETUP;
  m.maus = UI_MAUS_CURIOUS;
  m.setup.step = UI_SETUP_NEED_CODE;
  assert_page_turns("Pair me");
  start("amoled-175c");
  m.screen = UI_SCREEN_OFFLINE;
  m.maus = UI_MAUS_SLEEPING;
  strcpy(m.host_name, "Omkar's computer");
  m.offline.reason = UI_OFFLINE_HOST_UNREACHABLE;
  m.offline.retry_at_ms = 9000;
  assert_page_turns("Can't reach");
  stop();
}

/* Review Focus 3: before speak.end (total unknown) speaking pages by
 * UI_MS_PER_LINE per line, again with time alone. */
static void speaking_unknown_total_body(void) {
  m.screen = UI_SCREEN_SPEAKING;
  m.maus = UI_MAUS_SPEAKING;
  for (size_t i = 0; i < 40; i++) strcat(m.reply.text, "Word after word. ");
  m.reply.speak_total_ms = 0;
  m.reply.speak_elapsed_ms = 0;
  render(100);
  lv_obj_t *l = label_with("Word after word.");
  int32_t y0 = label_y(l);
  m.reply.speak_elapsed_ms = 30 * UI_MS_PER_LINE; /* past the first page on every board */
  ui_render(&m);
  TEST_ASSERT_LESS_THAN_INT32(y0, label_y(l));
}
static void test_speaking_pages_by_time_when_total_unknown(void) { each_board(speaking_unknown_total_body); }

/* Contract 2.9: ui_render is cheap when only levels and the speaking clock
 * move (core bumps rev for them every tick): no re-layout, no new text. */
static void render_cache_body(void) {
  m.screen = UI_SCREEN_SPEAKING;
  m.maus = UI_MAUS_SPEAKING;
  for (size_t i = 0; i < 40; i++) strcat(m.reply.text, "Word after word. ");
  m.reply.speak_total_ms = 20000;
  render(100);
  lv_obj_t *l = label_with("Word after word.");
  const char *text = lv_label_get_text(l);
  uint32_t applies = g_ui.applies;
  for (uint32_t i = 1; i <= 10; i++) {
    m.speak_level = (uint8_t)(i % 4);
    m.mic_level = (uint8_t)(i * 20);
    m.reply.speak_elapsed_ms = i * 1000;
    render(100 + 10 * i); /* rev++ each time */
  }
  TEST_ASSERT_EQUAL_UINT32(applies, g_ui.applies);
  TEST_ASSERT_EQUAL_PTR(text, lv_label_get_text(l));
  strcat(m.reply.text, "And more."); /* a real change is drawn */
  render(300);
  TEST_ASSERT_EQUAL_UINT32(applies + 1, g_ui.applies);
  TEST_ASSERT_TRUE(shows("And more."));
}
static void test_level_and_clock_changes_do_not_relayout(void) { each_board(render_cache_body); }

/* ---- the open item of spec 6.5: do A8 eyes draw white? ------------------ */

typedef struct {
  const void *src;
  lv_obj_t *found;
} img_find_t;

static void img_cb(lv_obj_t *o, void *ctx) {
  img_find_t *f = ctx;
  if (lv_obj_check_type(o, &lv_image_class) && visible(o) && lv_image_get_src(o) == f->src) f->found = o;
}

static void test_a8_eyes_draw_white(void) {
  start("amoled-175c");
  m.screen = UI_SCREEN_IDLE;
  m.maus = UI_MAUS_IDLE;
  render(0);
  const maus_art_t *art = maus_art_for(board->art_profile);
  const lv_image_dsc_t *eye = art->eyes[art->states[UI_MAUS_IDLE].pool[0]][0].img;
  img_find_t f = {eye, NULL};
  walk(lv_screen_active(), img_cb, &f);
  TEST_ASSERT_NOT_NULL_MESSAGE(f.found, "the idle Maus does not show its first expression with open eyes");
  /* a fully opaque pixel of the eye image */
  const uint8_t *alpha = eye->data + (eye->header.cf == LV_COLOR_FORMAT_A8 ? 0 : eye->header.w * eye->header.h * 2);
  int32_t ex = -1, ey = -1;
  for (int32_t y = 0; y < (int32_t)eye->header.h && ex < 0; y++) {
    for (int32_t x = 0; x < (int32_t)eye->header.w; x++) {
      if (alpha[y * (int32_t)eye->header.w + x] == 255) {
        ex = x;
        ey = y;
        break;
      }
    }
  }
  TEST_ASSERT_TRUE(ex >= 0);
  lv_area_t a;
  lv_obj_get_coords(f.found, &a);
  lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
  uint16_t px;
  memcpy(&px, buf->data + (uint32_t)(a.y1 + ey) * buf->header.stride + (uint32_t)(a.x1 + ex) * 2u, 2);
  TEST_ASSERT_EQUAL_HEX16_MESSAGE(0xFFFF, px, "A8 eye pixels must draw white (else switch palette.json eye_format)");
  stop();
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_idle);
  RUN_TEST(test_setup);
  RUN_TEST(test_offline);
  RUN_TEST(test_listening);
  RUN_TEST(test_thinking);
  RUN_TEST(test_reply);
  RUN_TEST(test_speaking);
  RUN_TEST(test_ask);
  RUN_TEST(test_card);
  RUN_TEST(test_update);
  RUN_TEST(test_image);
  RUN_TEST(test_toast);
  RUN_TEST(test_toast_covers_what_is_under_it);
  RUN_TEST(test_toast_never_cuts_text);
  RUN_TEST(test_long_unbroken_text_stays_on_screen);
  RUN_TEST(test_out_of_range_model_values_do_not_crash);
  RUN_TEST(test_ask_unlocks_without_a_model_change);
  RUN_TEST(test_image_without_pixels_draws_nothing);
  RUN_TEST(test_pages_turn_without_a_model_change);
  RUN_TEST(test_speaking_pages_by_time_when_total_unknown);
  RUN_TEST(test_level_and_clock_changes_do_not_relayout);
  RUN_TEST(test_a8_eyes_draw_white);
  return UNITY_END();
}
