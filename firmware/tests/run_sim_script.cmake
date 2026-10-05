# firmware/tests/run_sim_script.cmake
# SPDX-License-Identifier: Apache-2.0
# Runs one headless simulator script from a fresh state folder:
#   cmake -DSIM=<gadget-sim> -DBOARD=<id> -DSCRIPT=<file> -DSTATE=<dir>
#         [-DARGS="<more sim flags>"] [-DEXPECT=<regex stdout must match>] -P run_sim_script.cmake
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
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "gadget-sim exited with ${rc}")
endif()
if(EXPECT AND NOT out MATCHES "${EXPECT}")
  message(FATAL_ERROR "stdout does not match ${EXPECT}")
endif()
