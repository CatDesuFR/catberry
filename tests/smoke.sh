#!/bin/bash
# Opens the real window offscreen in a throwaway config folder, with and
# without settings that pick a keyboard key, and checks it opens and
# closes cleanly and keeps the settings.
#   tests/smoke.sh [path to catberry-ac]
set -eu
prog=$(realpath "${1:-./catberry-ac}")
cfg=$(mktemp -d)
trap 'rm -rf "$cfg"' EXIT
export XDG_CONFIG_HOME=$cfg QT_QPA_PLATFORM=offscreen
"$prog" gui --quit-after 800
mkdir -p "$cfg/catberry"
printf 'cps=25\ntarget=key\nkey=E\ntoggle_key=F7\n' >"$cfg/catberry/ac.conf"
"$prog" gui --quit-after 800
grep -q '^target=key$' "$cfg/catberry/ac.conf" && grep -q '^key=E$' "$cfg/catberry/ac.conf" &&
    grep -q '^toggle_key=F7$' "$cfg/catberry/ac.conf" && grep -q '^cps=25$' "$cfg/catberry/ac.conf"
echo "smoke: OK"
