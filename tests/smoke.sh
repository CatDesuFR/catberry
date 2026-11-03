#!/bin/bash
# Opens the real window offscreen with some saved macros and configs in a
# throwaway config folder, and checks it opens and closes cleanly.
#   tests/smoke.sh [path to catberry-mtools]
set -eu
prog=$(realpath "${1:-./catberry-mtools}")
cfg=$(mktemp -d)
trap 'rm -rf "$cfg"' EXIT
export XDG_CONFIG_HOME=$cfg QT_QPA_PLATFORM=offscreen
mkdir -p "$cfg/catberry/macros"
printf 'catberry-macro 1\nduration 2000\n0 LMB 1\n1000 LMB 0\n' >"$cfg/catberry/macros/test.macro"
printf 'repeat=2\nrecord_hotkey=F8\nplay_hotkey=F9\nname=test\n' >"$cfg/catberry/mtools.conf"
"$prog" -config x E,LMB F6 1 >/dev/null
"$prog" gui --quit-after 800
grep -q '^name=test$' "$cfg/catberry/mtools.conf" # settings written back
echo "smoke: OK"
