# firmware/tests/run_sim_script.cmake
# SPDX-License-Identifier: Apache-2.0
# Runs one headless simulator script from a fresh state folder:
#   cmake -DSIM=<gadget-sim> -DBOARD=<id> -DSCRIPT=<file> -DSTATE=<dir>
#         [-DARGS="<more sim flags>"] [-DEXPECT=<regex stdout must match>]
#         [-DRC=<expected exit code, default 0>] [-DNO_STATE=ON] -P run_sim_script.cmake
# NO_STATE checks that the run left no state folder behind.
if(NOT DEFINED RC)
  set(RC 0)
endif()
file(REMOVE_RECURSE "${STATE}")
separate_arguments(extra UNIX_COMMAND "${ARGS}")
execute_process(
  COMMAND "${SIM}" --board "${BOARD}" --headless --host script --state-dir "${STATE}" --script "${SCRIPT}" ${extra}
  INPUT_FILE /dev/null
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
  TIMEOUT 120)
message("${out}")
message("${err}")
if(NOT rc EQUAL RC)
  message(FATAL_ERROR "gadget-sim exited with ${rc}, expected ${RC}")
endif()
if(NO_STATE AND EXISTS "${STATE}")
  message(FATAL_ERROR "gadget-sim wrote ${STATE} before it read the script")
endif()
if(EXPECT AND NOT out MATCHES "${EXPECT}")
  message(FATAL_ERROR "stdout does not match ${EXPECT}")
endif()
