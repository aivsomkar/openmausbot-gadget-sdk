/* firmware/core/src/interaction.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Talking (spec §4.4, §5.4): presses, recording and mic frames, the turn in
 * flight and its heard / working / reply / done, typed `say` turns, cancel,
 * the 60 s limit and disconnects. One turn in flight at a time. */
#include <stdlib.h>
#include <string.h>
#include "core_internal.h"

#define TAG "turn"
#define PRE_FRAMES (GADGET_PRESS_MIN_MS / 20u) /* mic frames buffered before a press counts */

typedef enum { PRESS_NONE = 0, PRESS_TALK, PRESS_TOUCH } press_t;

static struct {
  press_t press;
  uint64_t press_at;
  int16_t x0, y0;
  bool moved;          /* the touch moved past the swipe threshold */
  bool rec;            /* voice.begin sent, frames streaming */
  char rec_turn[GADGET_TURN_MAX + 1];
  uint8_t rec_stream;
  uint8_t next_stream;
  uint32_t rec_frames;
  int16_t pre[PRE_FRAMES][GADGET_MIC_FRAME_SAMPLES];
  uint8_t pre_n;
  char turn[GADGET_TURN_MAX + 1]; /* the current turn: its frames are shown */
  bool in_flight;      /* sent, no done yet */
  bool stop_sent;
  bool reply_shown;
  uint64_t reply_until;  /* back to idle at this time; 0 = no timer */
} I;

/* Mean-square thresholds for -60 .. 0 dBFS in 1 dB steps, each at the half
 * dB below (32768^2 * 10^((dB - 0.5) / 10)) so levels round to the nearest dB.
 * Integer-only: identical on every platform. */
static const uint32_t DB_MS[61] = {
    957, 1205, 1517, 1909, 2404, 3026, 3810, 4796, 6038,
    7602, 9570, 12048, 15167, 19094, 24038, 30262, 38098, 47962,
    60381, 76015, 95697, 120476, 151670, 190941, 240381, 302622, 380978,
    479623, 603809, 760151, 956973, 1204758, 1516701, 1909413, 2403809, 3026216,
    3809780, 4796229, 6038094, 7601510, 9569734, 12047581, 15167006, 19094130, 24038085,
    30262156, 38097798, 47962285, 60380940, 76015100, 95697341, 120475814, 151670064, 190941298,
    240380852, 302621563, 380977976, 479622855, 603809400, 760150998, 956973408,
};

uint32_t core_mean_square(const int16_t *pcm, size_t n) {
  if (n == 0) return 0;
  uint64_t sum = 0;
  for (size_t i = 0; i < n; i++) sum += (uint64_t)((int32_t)pcm[i] * (int32_t)pcm[i]);
  return (uint32_t)(sum / n);
}

/* Linear from -60 dBFS (0) to 0 dBFS (255). */
static uint8_t mic_level(uint32_t ms) {
  int d = -1;
  for (int i = 0; i < 61; i++) {
    if (ms >= DB_MS[i]) d = i;
  }
  return d < 0 ? 0 : (uint8_t)(d * 255 / 60);
}

/* ---- sending --------------------------------------------------------------------- */

static void send_stop(const char *turn) {
  gp_stop_t s = {.turn = turn};
  session_send("stop", g_core_tx, gp_encode_stop(g_core_tx, sizeof g_core_tx, &s));
}

static void send_mic_frame(const int16_t *pcm) {
  uint8_t frame[2 + GADGET_MIC_FRAME_SAMPLES * 2];
  uint8_t payload[GADGET_MIC_FRAME_SAMPLES * 2];
  for (size_t i = 0; i < GADGET_MIC_FRAME_SAMPLES; i++) {
    uint16_t v = (uint16_t)pcm[i];
    payload[2 * i] = (uint8_t)(v & 0xff); /* little-endian on the wire */
    payload[2 * i + 1] = (uint8_t)(v >> 8);
  }
  size_t n = gp_bin_encode(frame, sizeof frame, GP_BIN_MIC, I.rec_stream, payload, sizeof payload);
  if (n > 0 && session_send_binary(frame, n) == GADGET_OK) I.rec_frames++;
}

/* ---- model ------------------------------------------------------------------------ */

static void clear_turn_model(void) {
  ui_model_t *m = &g_core.model;
  memset(&m->thinking, 0, sizeof m->thinking);
  memset(&m->reply, 0, sizeof m->reply);
  I.reply_shown = false;
  I.reply_until = 0;
}

static void publish(void) {
  ui_model_t *m = &g_core.model;
  g_core.f.recording = I.rec;
  g_core.f.turn_active = I.in_flight && m->reply.text[0] == '\0' && !m->reply.failed;
  g_core.f.reply_visible = I.reply_shown;
  if (!I.rec) {
    m->listening.countdown_s = 0;
    if (I.press == PRESS_NONE) m->mic_level = 0;
  }
}

/* ---- recording ---------------------------------------------------------------------- */

static void end_press(void) {
  if (I.press != PRESS_NONE) hal_mic_stop();
  I.press = PRESS_NONE;
  I.pre_n = 0;
  I.moved = false;
}

static void start_recording(void) {
  if (I.in_flight && !I.stop_sent) send_stop(I.turn); /* one turn in flight */
  I.in_flight = false;
  core_next_turn_id(I.rec_turn);
  memcpy(I.turn, I.rec_turn, sizeof I.turn);
  I.next_stream = (uint8_t)(I.next_stream % 255u + 1u);
  I.rec_stream = I.next_stream;
  I.rec_frames = 0;
  clear_turn_model();
  gp_voice_begin_t vb = {.turn = I.rec_turn, .stream = I.rec_stream, .rate = GADGET_MIC_RATE};
  if (session_send("voice.begin", g_core_tx, gp_encode_voice_begin(g_core_tx, sizeof g_core_tx, &vb)) != GADGET_OK) {
    end_press();
    return;
  }
  I.rec = true;
  g_core.model.listening.started_ms = I.press_at;
  for (uint8_t i = 0; i < I.pre_n; i++) send_mic_frame(I.pre[i]);
  I.pre_n = 0;
  hal_log(GADGET_LOG_INFO, TAG, "recording %s", I.rec_turn);
}

/* The caller's end_press() stops the mic, once. */
static void finish_recording(void) {
  gp_voice_end_t ve = {.turn = I.rec_turn, .ms = I.rec_frames * 20u};
  session_send("voice.end", g_core_tx, gp_encode_voice_end(g_core_tx, sizeof g_core_tx, &ve));
  I.rec = false;
  I.in_flight = true;
  I.stop_sent = false;
}

static void cancel_recording(void) {
  if (I.rec) {
    gp_voice_drop_t vd = {.turn = I.rec_turn};
    session_send("voice.drop", g_core_tx, gp_encode_voice_drop(g_core_tx, sizeof g_core_tx, &vd));
    I.rec = false;
    I.turn[0] = '\0';
  }
  end_press(); /* the release that follows finds no press and does nothing */
}

static void begin_press(press_t src, int16_t x, int16_t y) {
  if (I.press != PRESS_NONE) return;
  I.press = src;
  I.press_at = g_core.now;
  I.x0 = x;
  I.y0 = y;
  I.moved = false;
  I.pre_n = 0;
  if (hal_mic_start(GADGET_MIC_RATE) != GADGET_OK) hal_log(GADGET_LOG_ERROR, TAG, "mic did not start");
}

static void release_press(press_t src) {
  if (I.press != src) return;
  if (I.rec) finish_recording();
  end_press();
}

/* CANCEL, or a swipe down: the most recent thing gives way. */
static void cancel_action(void) {
  if (I.press != PRESS_NONE || I.rec) {
    cancel_recording();
    return;
  }
  if (I.in_flight) {
    if (!I.stop_sent) {
      send_stop(I.turn);
      I.stop_sent = true;
    }
    return;
  }
  if (I.reply_shown) clear_turn_model();
}

/* A touch shorter than the press minimum. */
static void tap(int16_t x, int16_t y) {
  (void)x;
  (void)y;
  if (I.reply_shown && !I.in_flight) clear_turn_model();
}

static int16_t swipe_threshold(void) { return (int16_t)(g_core.board->screen_h / 8); }

/* ---- module API ---------------------------------------------------------------------- */

void interaction_init(void) { memset(&I, 0, sizeof I); }

void interaction_deinit(void) {
  if (I.press != PRESS_NONE) hal_mic_stop();
  memset(&I, 0, sizeof I);
}

void interaction_input(const gadget_input_t *in) {
  bool ready = session_ready() && !g_core.f.ota_active;
  switch (in->type) {
    case GADGET_IN_TALK_DOWN:
      if (!session_ready()) {
        session_wake();
        break;
      }
      if (ready) begin_press(PRESS_TALK, 0, 0);
      break;
    case GADGET_IN_TALK_UP:
      release_press(PRESS_TALK);
      break;
    case GADGET_IN_TOUCH_DOWN:
      if (!session_ready()) {
        session_wake(); /* hold anywhere is TALK on touch boards (spec §5.4) */
        break;
      }
      if (ready) begin_press(PRESS_TOUCH, in->x, in->y);
      break;
    case GADGET_IN_TOUCH_MOVE:
      if (I.press == PRESS_TOUCH && !I.moved) {
        int dx = in->x - I.x0, dy = in->y - I.y0;
        int th = swipe_threshold();
        if (abs(dx) > th || abs(dy) > th) {
          I.moved = true;
          if (dy > th && dy > abs(dx)) { /* swipe down */
            if (!I.rec) end_press(); /* the swipe's own touch is not a recording: cancel what lies beneath */
            cancel_action();
          }
        }
      }
      break;
    case GADGET_IN_TOUCH_UP:
      if (I.press == PRESS_TOUCH) {
        bool short_press = !I.rec && !I.moved && g_core.now - I.press_at < GADGET_PRESS_MIN_MS;
        release_press(PRESS_TOUCH);
        if (short_press) tap(in->x, in->y);
      }
      break;
    case GADGET_IN_CANCEL_DOWN:
      cancel_action();
      break;
    case GADGET_IN_SWIPE:
      if (in->dir == GADGET_SWIPE_DOWN) cancel_action();
      break;
    default:
      break;
  }
  publish();
}

void interaction_mic(const gadget_mic_frame_t *f) {
  if (I.press == PRESS_NONE || f->samples != GADGET_MIC_FRAME_SAMPLES) return;
  g_core.model.mic_level = mic_level(core_mean_square(f->pcm, f->samples));
  if (I.rec) {
    send_mic_frame(f->pcm);
  } else if (I.pre_n < PRE_FRAMES) {
    memcpy(I.pre[I.pre_n++], f->pcm, sizeof I.pre[0]);
  } else {
    memmove(I.pre[0], I.pre[1], sizeof I.pre[0] * (PRE_FRAMES - 1)); /* keep the newest */
    memcpy(I.pre[PRE_FRAMES - 1], f->pcm, sizeof I.pre[0]);
  }
}

void interaction_tick(void) {
  uint64_t now = g_core.now;
  if (I.press != PRESS_NONE && !I.rec && !I.moved && now - I.press_at >= GADGET_PRESS_MIN_MS) {
    start_recording();
  }
  if (I.rec) {
    uint64_t elapsed = now - I.press_at;
    if (elapsed >= GADGET_UTTERANCE_MAX_MS) {
      hal_log(GADGET_LOG_INFO, TAG, "60 s limit reached");
      finish_recording();
      end_press(); /* the release that follows does nothing */
    } else if (elapsed + GADGET_COUNTDOWN_MS >= GADGET_UTTERANCE_MAX_MS) {
      g_core.model.listening.countdown_s = (uint8_t)((GADGET_UTTERANCE_MAX_MS - elapsed + 999u) / 1000u);
    }
  }
  if (I.reply_shown && !I.in_flight && I.reply_until != 0 && now >= I.reply_until) clear_turn_model();
  publish();
}

bool interaction_on_msg(const gp_msg_t *m) {
  ui_model_t *mod = &g_core.model;
  switch (m->op) {
    case GP_OP_HEARD:
      if (I.in_flight && strcmp(m->m.heard.turn, I.turn) == 0) {
        core_text_copy(mod->thinking.heard, sizeof mod->thinking.heard, m->m.heard.text);
      }
      break;
    case GP_OP_WORKING:
      if (I.in_flight && strcmp(m->m.working.turn, I.turn) == 0) {
        core_text_copy(mod->thinking.working, sizeof mod->thinking.working, m->m.working.text);
      }
      break;
    case GP_OP_REPLY:
      if (I.turn[0] != '\0' && strcmp(m->m.reply.turn, I.turn) == 0 && (I.in_flight || I.reply_shown)) {
        core_text_copy_tail(mod->reply.text, sizeof mod->reply.text, m->m.reply.text);
        mod->reply.final = m->m.reply.final;
        I.reply_shown = mod->reply.text[0] != '\0';
      }
      break;
    case GP_OP_DONE: {
      /* a turn is in flight from its voice.begin, so the host may end it while it still records (spec §4.4) */
      bool recording = I.rec && strcmp(m->m.done.turn, I.rec_turn) == 0;
      if (recording || (I.in_flight && strcmp(m->m.done.turn, I.turn) == 0)) {
        if (recording) {
          I.rec = false;
          end_press(); /* the mic stops; no voice.end */
        }
        I.in_flight = false;
        I.stop_sent = false;
        if (m->m.done.outcome == GP_OUTCOME_FAILED) {
          mod->reply.failed = true;
          core_text_copy(mod->reply.reason, sizeof mod->reply.reason, m->m.done.reason ? m->m.done.reason : "");
          I.reply_shown = true;
        } else {
          mod->reply.final = true;
          I.reply_shown = mod->reply.text[0] != '\0';
        }
        I.reply_until = g_core.now + GADGET_REPLY_IDLE_MS;
      }
      break;
    }
    default:
      return false;
  }
  publish();
  return true;
}

void interaction_on_session_lost(void) {
  bool had_turn = I.rec || I.in_flight;
  end_press();
  I.rec = false;
  I.in_flight = false;
  I.stop_sent = false;
  if (had_turn) {
    ui_model_t *m = &g_core.model;
    m->reply.failed = true;
    core_text_copy(m->reply.reason, sizeof m->reply.reason, "Connection lost");
    I.reply_shown = true;
    I.reply_until = g_core.now + GADGET_REPLY_IDLE_MS;
  }
  publish();
}

bool interaction_say(const char *text, char turn_out[GADGET_TURN_MAX + 1]) {
  if (!session_ready()) return false;
  if (I.press != PRESS_NONE || I.rec) cancel_recording();
  if (I.in_flight && !I.stop_sent) send_stop(I.turn);
  clear_turn_model();
  core_next_turn_id(I.turn);
  gp_say_t s = {.turn = I.turn, .text = text};
  if (session_send("say", g_core_tx, gp_encode_say(g_core_tx, sizeof g_core_tx, &s)) != GADGET_OK) return false;
  I.in_flight = true;
  I.stop_sent = false;
  core_text_copy(g_core.model.thinking.heard, sizeof g_core.model.thinking.heard, text);
  memcpy(turn_out, I.turn, GADGET_TURN_MAX + 1);
  publish();
  return true;
}

const char *interaction_turn(void) { return I.turn; }
bool interaction_turn_in_flight(void) { return I.in_flight; }
