/* SPDX-License-Identifier: Apache-2.0 */
/* LVGL 9.6.0 configuration for the desktop simulator and host tests only.
 * The ESP32 port configures LVGL through CONFIG_LV_* in sdkconfig.defaults
 * (P2c); ui_lv_requirements.h checks that both provide what the UI needs. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_USE_OS LV_OS_NONE
#define LV_DEF_REFR_PERIOD 33
#define LV_DRAW_SW_COMPLEX 1
#define LV_DRAW_SW_SUPPORT_RGB565A8 1
#define LV_DRAW_SW_SUPPORT_A8 1

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

/* The built-in ASCII font stays LVGL's default; the UI sets font_latin1_*
 * through styles on its root and widgets (never LV_USE_CUSTOM_FONT_DEFAULT). */
#define LV_FONT_MONTSERRAT_14 1

#define LV_USE_ANIMIMG 1

#if !defined(GADGET_LV_NO_SDL) /* set on lvgl when GADGET_WITH_SDL is OFF (headless-only build) */
#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
#define LV_SDL_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT
#define LV_SDL_BUF_COUNT 1
#define LV_SDL_ACCELERATED 0 /* software renderer: SDL dummy video (CI) has no accelerated one */
#define LV_SDL_FULLSCREEN 0
#define LV_SDL_DIRECT_EXIT 0 /* window close and quit never exit() the simulator */
#endif

#define LV_USE_TEST 1
#define LV_USE_TEST_SCREENSHOT_COMPARE 1
#ifndef LV_TEST_SCREENSHOT_CREATE_REFERENCE_IMAGE
#define LV_TEST_SCREENSHOT_CREATE_REFERENCE_IMAGE 0 /* -D...=1 on lvgl in a GADGET_SNAPSHOT_UPDATE build */
#endif
#define LV_USE_LODEPNG 1
#define LV_USE_FS_STDIO 1
#define LV_FS_STDIO_LETTER 'A'
#define LV_FS_DEFAULT_DRIVER_LETTER 'A' /* lodepng's file I/O goes through lv_fs in 9.6 */

#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif /* LV_CONF_H */
