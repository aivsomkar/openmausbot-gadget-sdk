#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Usage: tools/build-all.sh [board...]     (default: every directory in boards/)
# Builds each board in build/<board> with its own sdkconfig (spec §5.1), then
# runs check-size.sh and check-art-profile.sh, and fails when a standard build
# has test keys or NVS encryption on (those belong in their own -B directory).
# Needs ESP-IDF v6.0.3 active (idf.py on PATH). Extra idf.py arguments can be
# passed in IDF_ARGS.
set -euo pipefail

esp_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$esp_dir"
if (($# == 0)); then
  # shellcheck disable=SC2046
  set -- $(ls boards)
fi
mkdir -p build
for board in "$@"; do
  log="build/$board.log"
  echo "== $board"
  # shellcheck disable=SC2086
  if ! idf.py -B "build/$board" -D GADGET_BOARD="$board" -D SDKCONFIG="build/$board/sdkconfig" ${IDF_ARGS:-} build >"$log" 2>&1; then
    tail -n 60 "$log"
    echo "build-all: $board failed; full log in $esp_dir/$log" >&2
    exit 1
  fi
  tools/check-size.sh "$board"
  grep -qxF '# CONFIG_GADGET_TEST_KEYS is not set' "build/$board/sdkconfig" &&
    grep -qxF '# CONFIG_GADGET_NVS_ENCRYPT is not set' "build/$board/sdkconfig" ||
    { echo "build-all: $board has test keys or NVS encryption on" >&2; exit 1; }
  if [[ "${SKIP_ART_CHECK:-0}" != "1" ]]; then
    tools/check-art-profile.sh "$board"
  fi
done
