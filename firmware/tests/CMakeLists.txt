# firmware/tests/CMakeLists.txt
# SPDX-License-Identifier: Apache-2.0
# Native C tests (CTest). Labels: unit, vectors, e2e (P2b adds snapshot).

# A test of pure helpers that needs no HAL: test_<area>.c -> core.<area>.
function(gadget_pure_test area)
  add_executable(test_${area} test_${area}.c)
  target_link_libraries(test_${area} PRIVATE gadget_core unity::framework)
  gadget_warnings(test_${area})
  add_test(NAME core.${area} COMMAND test_${area} ${GADGET_VECTORS_DIR})
  set_tests_properties(core.${area} PROPERTIES LABELS unit TIMEOUT 60)
endfunction()

# A test of core behind the fake HAL (fake_hal.c): test_<area>.c -> core.<area>.
function(gadget_unit_test area)
  add_executable(test_${area} test_${area}.c fake_hal.c)
  target_link_libraries(test_${area} PRIVATE gadget_core unity::framework)
  gadget_warnings(test_${area})
  add_test(NAME core.${area} COMMAND test_${area} ${GADGET_VECTORS_DIR})
  set_tests_properties(core.${area} PROPERTIES LABELS unit TIMEOUT 60)
endfunction()

gadget_pure_test(util)
gadget_pure_test(proto)
gadget_pure_test(crypto)
gadget_pure_test(layout)
gadget_pure_test(console_parse)
gadget_unit_test(core_init)
gadget_unit_test(session)
gadget_unit_test(turns)
gadget_unit_test(console)
gadget_unit_test(audio)
gadget_unit_test(display)
gadget_unit_test(actions)
gadget_unit_test(ota)

# protocol/vectors in C (label vectors).
add_executable(test_vectors test_vectors.c)
target_link_libraries(test_vectors PRIVATE gadget_core unity::framework)
gadget_warnings(test_vectors)
add_test(NAME vectors.c COMMAND test_vectors ${GADGET_VECTORS_DIR})
set_tests_properties(vectors.c PROPERTIES LABELS vectors TIMEOUT 60)

# Headless simulator scripts on the scripted network (label unit).
if(GADGET_BUILD_SIM)
  function(gadget_sim_script name board)
    cmake_parse_arguments(S "" "EXPECT;ARGS" "" ${ARGN})
    add_test(NAME sim.${name}
             COMMAND ${CMAKE_COMMAND} -DSIM=$<TARGET_FILE:gadget-sim> -DBOARD=${board}
                     -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/sim_${name}.txt
                     -DSTATE=${CMAKE_CURRENT_BINARY_DIR}/sim-state/${name}
                     "-DARGS=${S_ARGS}" "-DEXPECT=${S_EXPECT}"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/run_sim_script.cmake)
    set_tests_properties(sim.${name} PROPERTIES LABELS unit TIMEOUT 150)
  endfunction()

  gadget_sim_script(handshake amoled-175c ARGS "--pair 123456" EXPECT "\"pair\":\"paired\"")
  gadget_sim_script(voice lcd-154 ARGS "--pair 123456")
  gadget_sim_script(ask amoled-175c ARGS "--pair 123456")
  # The fw checks read the script's final `@omb status` line: every boot line
  # carries a fw too. In a status line fw comes before the battery object, so
  # [^{]* stays within that line.
  gadget_sim_script(ota amoled-175c ARGS "--pair 123456" EXPECT "\"op\":\"status\"[^{]*\"fw\":\"1\\.1\\.0\"")
  gadget_sim_script(rollback amoled-175c ARGS "--pair 123456 --fail-probation --probation-ms 2000"
                    EXPECT "\"op\":\"status\"[^{]*\"fw\":\"0\\.0\\.0-dev\"")
  gadget_sim_script(reboot_rollback amoled-175c ARGS "--pair 123456"
                    EXPECT "\"op\":\"status\"[^{]*\"fw\":\"0\\.0\\.0-dev\"")

  # A script that cannot be read stops the simulator before it writes any state.
  add_test(NAME sim.missing_script
           COMMAND ${CMAKE_COMMAND} -DSIM=$<TARGET_FILE:gadget-sim> -DBOARD=lcd-154
                   -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/no_such_script.txt
                   -DSTATE=${CMAKE_CURRENT_BINARY_DIR}/sim-state/missing_script -DRC=1 -DNO_STATE=ON
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/run_sim_script.cmake)
  set_tests_properties(sim.missing_script PROPERTIES LABELS unit TIMEOUT 30)
endif()

# Simulator <-> fake host end-to-end scenarios (label e2e). They need Node
# >= 22.18 on PATH and `npm ci` at the repo root (the fake host imports ws).
# Without node the configure fails instead of dropping them silently;
# -DGADGET_E2E=OFF leaves them out on purpose.
if(GADGET_BUILD_SIM AND GADGET_E2E)
  find_program(GADGET_NODE node)
  if(GADGET_NODE)
    foreach(scenario enroll voice bargein bargein_after_done ask ota rollback)
      add_test(NAME e2e.${scenario}
               COMMAND ${GADGET_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run.ts --sim $<TARGET_FILE:gadget-sim>
                       ${CMAKE_CURRENT_SOURCE_DIR}/e2e/${scenario}.txt)
      set_tests_properties(e2e.${scenario} PROPERTIES LABELS e2e TIMEOUT 150)
    endforeach()
    # The simulator's upgrade checks against a raw TCP host (RFC 6455 §4.1).
    add_test(NAME e2e.upgrade
             COMMAND ${GADGET_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/upgrade.ts --sim $<TARGET_FILE:gadget-sim>)
    set_tests_properties(e2e.upgrade PROPERTIES LABELS e2e TIMEOUT 150)
  else()
    message(FATAL_ERROR "node >= 22.18 not found on PATH: install it or configure with -DGADGET_E2E=OFF")
  endif()
endif()

# ---- P2b: firmware/ui tests ------------------------------------------------
if(GADGET_WITH_LVGL)
  # gadget_ui_test(<area> [libs...]): test_ui_<area>.c -> CTest ui.<area>, label unit.
  function(gadget_ui_test area)
    add_executable(test_ui_${area} test_ui_${area}.c)
    target_link_libraries(test_ui_${area} PRIVATE gadget_ui unity::framework ${ARGN})
    target_compile_options(test_ui_${area} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ui.${area} COMMAND test_ui_${area})
    set_tests_properties(ui.${area} PROPERTIES LABELS unit TIMEOUT 120)
  endfunction()
  gadget_ui_test(lv_compat)
  gadget_ui_test(art)
endif()

if(GADGET_WITH_LVGL)
  gadget_ui_test(copy)
  gadget_ui_test(maus_engine)
endif()

if(GADGET_WITH_LVGL)
  gadget_ui_test(screens)
endif()

# ---- P2b: simulator display, SDL window and SDL audio tests ---------------
# Gated on the options, never on `TARGET`: gadget_sim_lvgl and gadget-sim come
# from ports/sim (added only with GADGET_BUILD_SIM), and target names resolve at
# generate time, so the order of add_subdirectory() calls does not matter.
if(GADGET_WITH_LVGL AND GADGET_BUILD_SIM)
  # gadget_sim_test(<area>): test_sim_<area>.c on SDL's dummy drivers -> CTest sim.<area>.
  function(gadget_sim_test area)
    add_executable(test_sim_${area} test_sim_${area}.c)
    target_link_libraries(test_sim_${area} PRIVATE gadget_sim_lvgl unity::framework)
    target_compile_definitions(test_sim_${area} PRIVATE _POSIX_C_SOURCE=200809L $<$<PLATFORM_ID:Darwin>:_DARWIN_C_SOURCE>)
    target_compile_options(test_sim_${area} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME sim.${area} COMMAND test_sim_${area})
    set_tests_properties(sim.${area} PROPERTIES LABELS unit TIMEOUT 60
                         ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
  endfunction()
  gadget_sim_test(display)
  if(GADGET_WITH_SDL)
    gadget_sim_test(sdl)
    gadget_sim_test(audio_sdl)
    # No usable audio: SDL audio does not start (unknown driver), or it starts
    # but neither device opens (the disk driver pointed at a missing folder).
    add_test(NAME sim.audio_sdl.no_device COMMAND test_sim_audio_sdl)
    set_tests_properties(sim.audio_sdl.no_device PROPERTIES LABELS unit TIMEOUT 60
                         ENVIRONMENT "SDL_AUDIODRIVER=gadget-none;GADGET_TEST_NO_AUDIO=1")
    set(_no_dir ${CMAKE_CURRENT_BINARY_DIR}/no-such-dir)
    add_test(NAME sim.audio_sdl.open_fails COMMAND test_sim_audio_sdl)
    set_tests_properties(sim.audio_sdl.open_fails PROPERTIES LABELS unit TIMEOUT 60
                         ENVIRONMENT "SDL_AUDIODRIVER=disk;SDL_DISKAUDIOFILE=${_no_dir}/out.raw;SDL_DISKAUDIOFILEIN=${_no_dir}/in.raw;GADGET_TEST_NO_AUDIO=1")
    # The whole simulator in window mode on SDL's dummy drivers, from a fresh state folder.
    set(_win_state ${CMAKE_BINARY_DIR}/snap-state/window-smoke)
    add_test(NAME sim.window_smoke.fresh
             COMMAND ${CMAKE_COMMAND} -DDIR=${_win_state} -P ${CMAKE_CURRENT_SOURCE_DIR}/fresh_dir.cmake)
    set_tests_properties(sim.window_smoke.fresh PROPERTIES FIXTURES_SETUP sim.window_smoke.state LABELS unit)
    add_test(NAME sim.window_smoke
             COMMAND $<TARGET_FILE:gadget-sim> --board amoled-175c --host script --state-dir ${_win_state}
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/window_smoke.txt)
    set_tests_properties(sim.window_smoke PROPERTIES FIXTURES_REQUIRED sim.window_smoke.state LABELS unit TIMEOUT 60
                         ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
  endif()
endif()

# ---- P2b: fixture snapshots (turn screens and copy variants) --------------
if(GADGET_WITH_LVGL AND GADGET_BUILD_SIM) # gadget_sim_lvgl lives in ports/sim
  add_executable(test_ui_snapshots test_ui_snapshots.c)
  target_link_libraries(test_ui_snapshots PRIVATE gadget_sim_lvgl unity::framework)
  target_compile_options(test_ui_snapshots PRIVATE -Wall -Wextra -Werror)
  foreach(board amoled-175c lcd-154 devkit)
    add_test(NAME ui.snap.fixtures.${board} COMMAND test_ui_snapshots ${board}
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(ui.snap.fixtures.${board} PROPERTIES LABELS snapshot TIMEOUT 120)
  endforeach()
endif()
