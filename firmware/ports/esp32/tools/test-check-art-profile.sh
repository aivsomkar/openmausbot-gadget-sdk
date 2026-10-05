#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Tests for check-art-profile.sh with a fake nm and fake build directories.
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cat >"$tmp/fake-nm" <<'NM'
#!/usr/bin/env bash
cat "$1.syms"
NM
chmod +x "$tmp/fake-nm"

make_build() { # <dir> <profile> <symbol lines...>
  local dir="$1" profile="$2"
  shift 2
  mkdir -p "$dir"
  : >"$dir/openmausbot-gadget.elf"
  printf 'CONFIG_GADGET_ART_PROFILE="%s"\n' "$profile" >"$dir/sdkconfig"
  printf '%s\n' "$@" >"$dir/openmausbot-gadget.elf.syms"
}
expect() { # <want exit> <name> <args...>
  local want="$1" name="$2"
  shift 2
  NM="$tmp/fake-nm" "$here/check-art-profile.sh" "$@" >"$tmp/out" 2>&1
  local got=$?
  if [[ "$got" != "$want" ]]; then
    echo "FAIL $name: exit $got, want $want"
    cat "$tmp/out"
    exit 1
  fi
  echo "ok $name"
}

make_build "$tmp/good" s240 "3c0a1000 R maus_s240_body" "3c0a2000 R maus_s240_eye_6_0" "42001000 T app_main"
expect 0 "only the board's profile" amoled-175c "$tmp/good"

make_build "$tmp/both" s150 "3c0a1000 R maus_s150_body" "3c0b1000 R maus_s240_body"
expect 1 "both profiles linked" lcd-154 "$tmp/both"

make_build "$tmp/none" s150 "42001000 T app_main"
expect 1 "own profile missing" devkit "$tmp/none"

make_build "$tmp/noprofile" "" "3c0a1000 R maus_s240_body"
expect 1 "no profile in sdkconfig" amoled-175 "$tmp/noprofile"
echo "all check-art-profile tests passed"
