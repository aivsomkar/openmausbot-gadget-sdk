#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Tests for check-size.sh with fake build directories. Prints "ok" lines and
# exits non-zero on the first wrong result.
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

good_cfg() {
  printf '%s\n' 'CONFIG_IDF_TARGET="esp32s3"' 'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y' \
    '# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set' >"$1/sdkconfig"
}
make_build() { # <dir> <app bytes>
  mkdir -p "$1"
  head -c "$2" /dev/zero >"$1/openmausbot-gadget.bin"
  good_cfg "$1"
}
expect() { # <want exit> <name> <args...>
  local want="$1" name="$2"
  shift 2
  "$here/check-size.sh" "$@" >"$tmp/out" 2>&1
  local got=$?
  if [[ "$got" != "$want" ]]; then
    echo "FAIL $name: exit $got, want $want"
    cat "$tmp/out"
    exit 1
  fi
  echo "ok $name"
}

make_build "$tmp/exact" 6291456
expect 0 "app exactly the slot size" amoled-175c "$tmp/exact"
grep -q 'check-size: amoled-175c ok, 6291456 of 6291456 bytes (100%)' "$tmp/out" || { echo "FAIL summary line"; exit 1; }

make_build "$tmp/big" 6291457
expect 1 "app one byte over the slot" amoled-175c "$tmp/big"

make_build "$tmp/norollback" 1000
printf '%s\n' '# CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is not set' >"$tmp/norollback/sdkconfig"
expect 1 "rollback off" lcd-154 "$tmp/norollback"

make_build "$tmp/antirollback" 1000
printf '%s\n' 'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y' 'CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y' >"$tmp/antirollback/sdkconfig"
expect 1 "anti-rollback on" devkit "$tmp/antirollback"

mkdir -p "$tmp/nobin"
good_cfg "$tmp/nobin"
expect 1 "no app image" amoled-175 "$tmp/nobin"

expect 1 "no board argument"
echo "all check-size tests passed"
