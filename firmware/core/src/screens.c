/* firmware/core/src/screens.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Screen and Maus-state selection (contract §2.7): core decides, the UI only
 * draws model.screen. Reads the flags each module keeps in g_core.f. */
#include "core_internal.h"

static ui_screen_t pick_screen(void) {
  if (!session_ready()) return session_is_setup() ? UI_SCREEN_SETUP : UI_SCREEN_OFFLINE;
  if (g_core.f.ota_active) return UI_SCREEN_UPDATE;
  if (g_core.f.recording) return UI_SCREEN_LISTENING;
  if (g_core.f.ask_visible) return UI_SCREEN_ASK;
  if (g_core.f.speaking) return UI_SCREEN_SPEAKING;
  if (g_core.f.turn_active) return UI_SCREEN_THINKING;
  if (g_core.f.reply_visible) return UI_SCREEN_REPLY;
  if (g_core.f.image_visible) return UI_SCREEN_IMAGE;
  if (g_core.f.card_visible) return UI_SCREEN_CARD;
  return UI_SCREEN_IDLE;
}

static ui_maus_state_t maus_for(ui_screen_t s, const ui_model_t *m) {
  switch (s) {
    case UI_SCREEN_SETUP: return UI_MAUS_CURIOUS;
    case UI_SCREEN_OFFLINE: return UI_MAUS_SLEEPING;
    case UI_SCREEN_IDLE: return UI_MAUS_IDLE;
    case UI_SCREEN_LISTENING: return UI_MAUS_LISTENING;
    case UI_SCREEN_THINKING: return m->thinking.working[0] ? UI_MAUS_WORKING : UI_MAUS_THINKING;
    case UI_SCREEN_SPEAKING: return UI_MAUS_SPEAKING;
    case UI_SCREEN_REPLY: return m->reply.failed ? UI_MAUS_ALERTING : UI_MAUS_IDLE;
    default: return UI_MAUS_NONE; /* boot, ask, card, image, update */
  }
}

void screens_update(void) {
  ui_model_t *m = &g_core.model;
  m->screen = pick_screen();
  m->maus = maus_for(m->screen, m);
  if (g_core.f.toast_visible && m->maus != UI_MAUS_NONE) m->maus = UI_MAUS_NOTIFYING;
}
