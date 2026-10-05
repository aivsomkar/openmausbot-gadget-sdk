# firmware/ports/sim/CMakeLists.txt
# SPDX-License-Identifier: Apache-2.0
# gadget-sim: the desktop simulator. P2a builds it without LVGL (null display,
# ui_stub.c); plan P2b adds sim_display_lvgl.c, sim_sdl.c and sim_audio_sdl.c.
add_executable(gadget-sim
  main.c
  sim_args.c
  sim_events.c
  sim_storage.c
  sim_system.c
  sim_wifi.c
  sim_battery.c
  sim_console.c
  sim_audio.c
  sim_audio_file.c
  sim_mdns.c
  sim_ota.c
  sim_ws.c
  sim_net_script.c
  sim_script.c
  sim_display_null.c
  ui_stub.c
)
target_include_directories(gadget-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gadget-sim PRIVATE gadget_core wslay)
# -std=c11 hides POSIX (poll, getaddrinfo, clock_gettime, strdup) on Linux.
target_compile_definitions(gadget-sim PRIVATE _POSIX_C_SOURCE=200809L GADGET_SIM_VERSION="${GADGET_SIM_VERSION}")
if(APPLE)
  target_compile_definitions(gadget-sim PRIVATE _DARWIN_C_SOURCE)
endif()
find_package(Threads REQUIRED)
target_link_libraries(gadget-sim PRIVATE Threads::Threads)
gadget_warnings(gadget-sim)

# ---- P2b: the LVGL display (headless + SDL window) and SDL audio ----------
if(GADGET_WITH_LVGL)
  set(_sim_lvgl_srcs sim_display_lvgl.c)
  if(GADGET_WITH_SDL)
    list(APPEND _sim_lvgl_srcs sim_sdl.c sim_audio_sdl.c)
  endif()
  add_library(gadget_sim_lvgl STATIC ${_sim_lvgl_srcs})
  target_include_directories(gadget_sim_lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(gadget_sim_lvgl PUBLIC gadget_ui gadget_core lvgl::lvgl)
  target_compile_definitions(gadget_sim_lvgl PRIVATE _POSIX_C_SOURCE=200809L $<$<PLATFORM_ID:Darwin>:_DARWIN_C_SOURCE>)
  set_target_properties(gadget_sim_lvgl PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_compile_options(gadget_sim_lvgl PRIVATE -Wall -Wextra -Wpedantic -Werror)
  if(GADGET_WITH_SDL)
    target_link_libraries(gadget_sim_lvgl PUBLIC SDL2::SDL2)
    target_compile_definitions(gadget_sim_lvgl PUBLIC GADGET_WITH_SDL=1)
  endif()
  if(GADGET_SNAPSHOT_UPDATE)
    target_compile_definitions(gadget_sim_lvgl PUBLIC GADGET_SNAPSHOT_UPDATE=1)
  endif()
  # Exactly one display and one UI are linked (contract 2.16): drop the null
  # display and the UI stub from gadget-sim and link the LVGL ones.
  get_target_property(_sim_srcs gadget-sim SOURCES)
  list(FILTER _sim_srcs EXCLUDE REGEX "(^|/)(sim_display_null|ui_stub)\\.c$")
  set_property(TARGET gadget-sim PROPERTY SOURCES ${_sim_srcs})
  target_link_libraries(gadget-sim PRIVATE gadget_sim_lvgl)
endif()
