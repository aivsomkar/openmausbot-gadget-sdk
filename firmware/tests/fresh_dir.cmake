# SPDX-License-Identifier: Apache-2.0
# cmake -DDIR=<path> [-DDEV_KEY=<64 lowercase hex>] -P fresh_dir.cmake
# An empty state folder at <path>. With DEV_KEY, the folder's storage.json
# (contract 4.6) holds only that device key, so the simulator keeps a fixed
# identity instead of generating a random one: the RFC key of contract 1.7
# gives the device id gad_b18b86ce1389e46d.
if(NOT DIR)
  message(FATAL_ERROR "fresh_dir.cmake: pass -DDIR=<path>")
endif()
file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
if(DEV_KEY)
  if(NOT DEV_KEY MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "fresh_dir.cmake: DEV_KEY must be lowercase hex")
  endif()
  file(WRITE "${DIR}/storage.json" "{\"version\":1,\"entries\":{\"dev_key\":{\"blob\":\"${DEV_KEY}\"}}}\n")
endif()
