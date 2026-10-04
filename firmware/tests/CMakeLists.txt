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

gadget_pure_test(util)
gadget_pure_test(proto)
gadget_pure_test(crypto)

# protocol/vectors in C (label vectors).
add_executable(test_vectors test_vectors.c)
target_link_libraries(test_vectors PRIVATE gadget_core unity::framework)
gadget_warnings(test_vectors)
add_test(NAME vectors.c COMMAND test_vectors ${GADGET_VECTORS_DIR})
set_tests_properties(vectors.c PROPERTIES LABELS vectors TIMEOUT 60)
