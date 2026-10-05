#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Usage: tools/check-size.sh <board> [build-dir]
# Fails when build/<board>/openmausbot-gadget.bin does not fit the 6 MiB OTA
# slot (caps.ota.max), or when build/<board>/sdkconfig lost app rollback or
# turned on anti-rollback (spec §4.8, contract §2.17). The build directory
# defaults to build/<board> next to this script's parent directory.
set -euo pipefail

SLOT_BYTES=6291456
board="${1:?usage: check-size.sh <board> [build-dir]}"
esp_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${2:-$esp_dir/build/$board}"
bin="$build_dir/openmausbot-gadget.bin"
cfg="$build_dir/sdkconfig"

if [[ ! -f "$bin" ]]; then
  echo "check-size: $bin not found; build $board first" >&2
  exit 1
fi
if [[ ! -f "$cfg" ]]; then
  echo "check-size: $cfg not found" >&2
  exit 1
fi

fail=0
size=$(wc -c <"$bin" | tr -d ' ')
if ((size > SLOT_BYTES)); then
  echo "check-size: $board app is $size bytes, over the $SLOT_BYTES-byte OTA slot" >&2
  fail=1
fi
for line in 'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y' '# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set'; do
  if ! grep -qxF "$line" "$cfg"; then
    echo "check-size: $cfg lacks the line: $line" >&2
    fail=1
  fi
done
if ((fail == 0)); then
  echo "check-size: $board ok, $size of $SLOT_BYTES bytes ($((size * 100 / SLOT_BYTES))%)"
fi
exit "$fail"
