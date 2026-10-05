#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Usage: tools/check-art-profile.sh <board> [build-dir]
# An ESP32 image links only its board's Maus art profile (contract §2.15):
# the ELF must define maus_<profile>_body and no maus_<other>_* symbol.
# NM overrides the symbol lister (default: xtensa-esp32s3-elf-nm).
set -euo pipefail

board="${1:?usage: check-art-profile.sh <board> [build-dir]}"
esp_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${2:-$esp_dir/build/$board}"
nm_tool="${NM:-xtensa-esp32s3-elf-nm}"
elf="$build_dir/openmausbot-gadget.elf"
cfg="$build_dir/sdkconfig"

[[ -f "$elf" ]] || { echo "check-art-profile: $elf not found" >&2; exit 1; }
[[ -f "$cfg" ]] || { echo "check-art-profile: $cfg not found" >&2; exit 1; }
profile=$(sed -n 's/^CONFIG_GADGET_ART_PROFILE="\(s[0-9]*\)"$/\1/p' "$cfg")
case "$profile" in
  s240) other=s150 ;;
  s150) other=s240 ;;
  *) echo "check-art-profile: no CONFIG_GADGET_ART_PROFILE in $cfg" >&2; exit 1 ;;
esac

symbols=$("$nm_tool" "$elf")
if ! grep -q " maus_${profile}_body\$" <<<"$symbols"; then
  echo "check-art-profile: $board does not link maus_${profile}_body" >&2
  exit 1
fi
stray=$(grep -c " maus_${other}_" <<<"$symbols" || true)
if [[ "$stray" != "0" ]]; then
  echo "check-art-profile: $board links $stray maus_${other}_* symbols; only $profile belongs in this image" >&2
  exit 1
fi
echo "check-art-profile: $board links only $profile"
