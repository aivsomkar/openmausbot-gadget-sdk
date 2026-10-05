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
