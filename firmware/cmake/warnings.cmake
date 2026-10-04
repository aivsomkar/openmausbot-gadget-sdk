# firmware/cmake/warnings.cmake
# SPDX-License-Identifier: Apache-2.0
# Warnings as errors on our own targets (never on fetched dependencies),
# plus the optional sanitizers.
function(gadget_warnings target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  if(GADGET_SANITIZE)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
