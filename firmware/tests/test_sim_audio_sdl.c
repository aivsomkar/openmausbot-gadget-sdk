/* SPDX-License-Identifier: Apache-2.0 */
/* The SDL audio backend on SDL's dummy audio driver (CTest sets
 * SDL_AUDIODRIVER=dummy): the lazy mic and its permission hint, rates, queue
 * limits, stop, mic frames; and silence without a usable device. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL2/SDL.h>

#include "sim_hal.h"
#include "unity.h"

static int mic_frames;
static int bad_frames;
static int noisy_frames; /* the dummy driver records silence: any other sample is not from SDL */

void sim_post_event(const gadget_event_t *ev) {
  if (ev->type != GADGET_EV_MIC_FRAME) return;
  if (ev->u.mic.samples != GADGET_MIC_FRAME_SAMPLES || !ev->u.mic.pcm) {
    bad_frames++;
    return;
  }
  mic_frames++;
  for (size_t i = 0; i < GADGET_MIC_FRAME_SAMPLES; i++) {
    if (ev->u.mic.pcm[i] != 0) {
      noisy_frames++;
      break;
    }
  }
}

static const sim_audio_backend_t *b;

void setUp(void) {
  b = sim_audio_sdl_backend();
  mic_frames = 0;
  bad_frames = 0;
  noisy_frames = 0;
}
void tearDown(void) {
  b->mic_stop();
  b->spk_stop();
}

/* Runs first, so nothing in this process has touched SDL audio yet. */
static void test_mic_opens_lazily_and_hints_on_silence(void) {
  TEST_ASSERT_EQUAL_UINT32(0, SDL_WasInit(SDL_INIT_AUDIO)); /* selecting the backend opens nothing */
  FILE *log = tmpfile();
  TEST_ASSERT_NOT_NULL(log);
  fflush(stderr);
  int saved = dup(fileno(stderr));
  dup2(fileno(log), fileno(stderr));
  gadget_status_t st = b->mic_start(GADGET_MIC_RATE); /* the first TALK */
  for (int i = 0; i < 100; i++) {                    /* 2 s of the dummy driver's silence */
    SDL_Delay(20);
    b->pump(0);
  }
  b->mic_stop();
  fflush(stderr);
  dup2(saved, fileno(stderr));
  close(saved);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, st);
  TEST_ASSERT_NOT_EQUAL(0, SDL_WasInit(SDL_INIT_AUDIO));
  char text[4096];
  /* stderr wrote through the descriptor, behind log's stdio buffer: read it the same way */
  lseek(fileno(log), 0, SEEK_SET);
  ssize_t n = read(fileno(log), text, sizeof text - 1);
  fclose(log);
  text[n > 0 ? n : 0] = '\0';
  int hints = 0;
  for (const char *p = text; (p = strstr(p, "the microphone is silent")) != NULL; p++) hints++;
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, hints, "the permission hint prints once after about 1 s of silence");
  TEST_ASSERT_GREATER_OR_EQUAL(50, mic_frames); /* silent frames still reach core */
  TEST_ASSERT_EQUAL_INT(0, noisy_frames);
}

static void test_rates_outside_v1_are_refused(void) {
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_UNSUPPORTED, b->mic_start(24000));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_UNSUPPORTED, b->spk_open(44100));
}

static void test_speaker_queues_reports_and_stops(void) {
  static int16_t pcm[1600]; /* 100 ms at 16 kHz */
  for (size_t i = 0; i < 1600; i++) pcm[i] = (int16_t)((i % 32) * 500 - 8000);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, b->spk_open(16000));
  b->spk_stop();
  TEST_ASSERT_EQUAL_size_t(1600, b->spk_write(pcm, 1600));
  uint32_t ms = b->spk_buffered_ms();
  TEST_ASSERT_TRUE(ms > 50 && ms <= 100);
  b->spk_stop();
  TEST_ASSERT_EQUAL_UINT32(0, b->spk_buffered_ms());
}

static void test_speaker_never_queues_more_than_two_seconds(void) {
  static int16_t pcm[24000];
  TEST_ASSERT_EQUAL_INT(GADGET_OK, b->spk_open(24000));
  b->spk_stop();
  TEST_ASSERT_EQUAL_size_t(24000, b->spk_write(pcm, 24000)); /* 1 s */
  TEST_ASSERT_EQUAL_size_t(24000, b->spk_write(pcm, 24000)); /* 2 s */
  TEST_ASSERT_TRUE(b->spk_write(pcm, 24000) < 24000);        /* only what played meanwhile */
  TEST_ASSERT_TRUE(b->spk_buffered_ms() <= 2000);
}

static void test_mic_posts_20_ms_frames_after_talk(void) {
  TEST_ASSERT_EQUAL_INT(GADGET_OK, b->mic_start(GADGET_MIC_RATE));
  for (int i = 0; i < 40; i++) { /* ~400 ms of real time */
    SDL_Delay(10);
    b->pump(0);
  }
  b->mic_stop();
  TEST_ASSERT_GREATER_THAN(5, mic_frames);
  TEST_ASSERT_EQUAL_INT(0, bad_frames);
  TEST_ASSERT_EQUAL_INT(0, noisy_frames); /* whole frames only, even when SDL hands over part of one */
  int after = mic_frames;
  SDL_Delay(50);
  b->pump(0);
  TEST_ASSERT_EQUAL_INT(after, mic_frames); /* nothing once stopped */
}

/* Run alone (GADGET_TEST_NO_AUDIO) twice: with SDL_AUDIODRIVER naming a driver
 * that does not exist (SDL audio does not start), and with SDL's disk driver
 * pointed at a missing folder (SDL starts, but neither device opens). */
static void test_no_audio_device_is_silent_not_fatal(void) {
  static int16_t pcm[320];
  TEST_ASSERT_EQUAL_INT(GADGET_OK, b->mic_start(GADGET_MIC_RATE));
  SDL_Delay(100);
  b->pump(0);
  TEST_ASSERT_EQUAL_INT(0, mic_frames);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, b->spk_open(16000));
  TEST_ASSERT_EQUAL_size_t(320, b->spk_write(pcm, 320)); /* accepted and dropped */
  TEST_ASSERT_EQUAL_UINT32(0, b->spk_buffered_ms());
}

int main(void) {
  UNITY_BEGIN();
  if (getenv("GADGET_TEST_NO_AUDIO")) {
    RUN_TEST(test_no_audio_device_is_silent_not_fatal);
    return UNITY_END();
  }
  RUN_TEST(test_mic_opens_lazily_and_hints_on_silence); /* first: SDL audio still untouched */
  RUN_TEST(test_rates_outside_v1_are_refused);
  RUN_TEST(test_speaker_queues_reports_and_stops);
  RUN_TEST(test_speaker_never_queues_more_than_two_seconds);
  RUN_TEST(test_mic_posts_20_ms_frames_after_talk);
  int r = UNITY_END();
  SDL_Quit();
  return r;
}
