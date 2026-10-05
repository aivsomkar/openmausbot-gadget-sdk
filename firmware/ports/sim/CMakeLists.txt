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
target_link_libraries(gadget-sim PRIVATE gadget_core)
# -std=c11 hides POSIX (poll, getaddrinfo, clock_gettime, strdup) on Linux.
target_compile_definitions(gadget-sim PRIVATE _POSIX_C_SOURCE=200809L GADGET_SIM_VERSION="${GADGET_SIM_VERSION}")
if(APPLE)
  target_compile_definitions(gadget-sim PRIVATE _DARWIN_C_SOURCE)
endif()
find_package(Threads REQUIRED)
target_link_libraries(gadget-sim PRIVATE Threads::Threads)
gadget_warnings(gadget-sim)
