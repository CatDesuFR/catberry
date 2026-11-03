#!/bin/bash
# Checks catberry install / uninstall / versions for real, inside a fresh
# container of one distro, as root. Started by ci/test-install-local.sh:
#   CATBERRY_RELEASES=<url of a copy of the releases> ci/test-install.sh <target> <launcher package dir>
# The launcher package is the one being tested (built from this tree).
set -uo pipefail
target=$1
pkgdir=$(realpath "$2")
: "${CATBERRY_RELEASES:?}"

pass=0 fail=0
ok() { echo "ok   $*"; pass=$((pass + 1)); }
bad() { echo "FAIL $*"; fail=$((fail + 1)); }
# expect <description> <command...>: the command must succeed.
expect() { local what=$1; shift; if "$@"; then ok "$what"; else bad "$what"; fi; }
# Runs catberry, answering the package manager's own questions with their
# default (Enter); dnf's default is No, so it gets "y". Exit code is catberry's.
answer=""
[ "$target" = fedora ] && answer=y
cb() { catberry "$@" 2>&1 < <(yes "$answer"); }

has_pkg() {
    case "$target" in
    arch) pacman -Q "$1" >/dev/null 2>&1 ;;
    fedora) rpm -q "$1" >/dev/null 2>&1 ;;
    *) [ "$(dpkg-query -W -f='${Status}' "$1" 2>/dev/null)" = "install ok installed" ] ;;
    esac
}
no_pkg() { ! has_pkg "$1"; }
remove_pkg() {
    case "$target" in
    arch) pacman -Rdd --noconfirm "$1" ;;
    fedora) rpm -e --nodeps "$1" ;;
    *) dpkg -r --force-depends "$1" ;;
    esac >/dev/null 2>&1
}

echo "== setup: install the launcher being tested"
shopt -s nullglob
case "$target" in
arch) pacman -Syu --noconfirm >/dev/null && pacman -U --noconfirm "$pkgdir"/*.pkg.tar.zst >/dev/null ;;
fedora) dnf install -y -q "$pkgdir"/*.rpm >/dev/null ;;
*) apt-get update -qq >/dev/null && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq curl ca-certificates \
       "$pkgdir"/*+"${target}"_amd64.deb >/dev/null ;;
esac || { echo "setup failed"; exit 1; }
command -v curl >/dev/null || { echo "setup: no curl"; exit 1; }
export DEBIAN_FRONTEND=noninteractive

echo "== versions"
out=$(cb versions roofce)
expect "versions roofce lists 1.2.2" grep -q "1\.2\.2" <<<"$out"
out=$(cb ac versions)
expect "ac versions (other order) lists 1.1.2" grep -q "1\.1\.2" <<<"$out"

echo "== install pulls in the launcher package when it is missing"
# Keep the launcher under test as a plain file, without its package.
cp /usr/bin/catberry /usr/local/bin/catberry
remove_pkg catberry
expect "launcher package removed for the test" no_pkg catberry
out=$(cb install ac)
echo "$out" | sed 's/^/     | /' | tail -5
expect "install ac succeeds" grep -q "ac 1.1.2 installed" <<<"$out"
expect "catberry-ac package installed" has_pkg catberry-ac
expect "catberry package installed first" has_pkg catberry

echo "== same version again"
out=$(cb install ac); code=$?
expect "already installed message" grep -q "ac 1.1.2 is already installed." <<<"$out"
expect "already installed exits 0" test "$code" = 0
rm -f /usr/bin/catberry-ac # broken install: the program file is gone
out=$(cb ac install --reinstall); code=$?
expect "--reinstall exits 0" test "$code" = 0
expect "--reinstall installs" grep -q "ac 1.1.2 installed" <<<"$out"
expect "--reinstall puts the program back" test -x /usr/bin/catberry-ac

echo "== unknown version"
out=$(cb install ac 9.9); code=$?
expect "unknown version message" grep -q "ac has no version 9.9. Published: 1.1.2" <<<"$out"
expect "unknown version exits 1" test "$code" = 1

echo "== damaged download"
out=$(CATBERRY_RELEASES="$CATBERRY_RELEASES/damaged" cb install mtools); code=$?
expect "damaged download message" grep -q "download damaged, nothing was installed" <<<"$out"
expect "damaged download exits 1" test "$code" = 1
expect "nothing installed after damaged download" no_pkg catberry-mtools

echo "== roofce"
out=$(cb install roofce); code=$?
echo "$out" | tail -3 | sed 's/^/     | /'
expect "install roofce succeeds" test "$code" = 0
expect "catberry-roofce package installed" has_pkg catberry-roofce
expect "roofce restart hint" grep -q "catberry roofce stop; catberry roofce start" <<<"$out"

# Uninstall reads its own answers from stdin, one line each; no line = No.
un() { printf "$1" | catberry "${@:2}" 2>&1; }
cfg=/root/.config share=/root/.local/share

echo "== uninstall refuses to run under sudo"
out=$(SUDO_USER=someone un 'y\n' uninstall ac); code=$?
expect "sudo uninstall refused" grep -q "Run it without sudo" <<<"$out"
expect "sudo uninstall exits 1" test "$code" = 1
expect "sudo uninstall removed nothing" has_pkg catberry-ac

echo "== uninstall ac, keeping files"
mkdir -p $cfg/catberry && echo x >$cfg/catberry/ac.conf
out=$(un '\n' uninstall ac); code=$?
expect "uninstall ac exits 0" test "$code" = 0
expect "catberry-ac package removed" no_pkg catberry-ac
expect "ac.conf kept on Enter" test -f $cfg/catberry/ac.conf
expect "says ac uninstalled" grep -q "^ac uninstalled." <<<"$out"
expect "says files were kept" grep -q "Your settings and files were kept." <<<"$out"

echo "== uninstall ac again: not installed, files still offered"
out=$(un 'y\n' ac uninstall); code=$?
expect "not installed message" grep -q "ac is not installed." <<<"$out"
expect "not installed exits 0" test "$code" = 0
expect "ac.conf deleted on y" test ! -e $cfg/catberry/ac.conf

echo "== uninstall roofce: every copy, then its files"
mkdir -p /root/.local/bin $cfg/autostart $cfg/catberry $share/catberry/roofce/wallpapers /root/.cache/catberry/roofce
printf '#!/bin/sh\n' >/root/.local/bin/catberry-roofce && chmod +x /root/.local/bin/catberry-roofce
echo x >$cfg/autostart/catberry-roofce.desktop
mkdir -p $share/applications && echo x >$share/applications/catberry-roofce.desktop
echo x >$cfg/catberry/roofce.conf
echo x >$share/catberry/roofce/wallpapers/a.png
out=$(un 'n\n' uninstall roofce)
expect "catberry-roofce package removed" no_pkg catberry-roofce
expect "~/.local/bin copy removed" test ! -e /root/.local/bin/catberry-roofce
expect "autostart entry removed" test ! -e $cfg/autostart/catberry-roofce.desktop
expect "make install's app-menu entry removed" test ! -e $share/applications/catberry-roofce.desktop
expect "roofce.conf kept on n" test -f $cfg/catberry/roofce.conf
expect "lists the wallpapers folder" grep -q "$share/catberry/roofce" <<<"$out"
out=$(un 'y\n' uninstall roofce)
expect "roofce files deleted on y" test ! -e $cfg/catberry/roofce.conf -a ! -e $share/catberry/roofce
expect "empty catberry data folder removed" test ! -e $share/catberry
expect "empty catberry config folder removed" test ! -e $cfg/catberry

echo "== uninstall catberry"
cb install ac >/dev/null; cb install mtools >/dev/null
mkdir -p $cfg/catberry/macros && echo x >$cfg/catberry/macros/m.macro
out=$(un 'n\n' uninstall catberry)
expect "No: nothing removed" has_pkg catberry-ac
expect "asks about ac and mtools" grep -q "ac and mtools need the launcher and will be removed too. Continue?" <<<"$out"
out=$(un 'y\nn\n' catberry uninstall); code=$?
expect "uninstall catberry exits 0" test "$code" = 0
expect "catberry-ac removed with the launcher" no_pkg catberry-ac
expect "catberry-mtools removed with the launcher" no_pkg catberry-mtools
expect "catberry package removed" no_pkg catberry
expect "macros kept on n" test -f $cfg/catberry/macros/m.macro

echo "== unsupported system"
# /etc/os-release is often a link to /usr/lib/os-release; writing through it
# and copying back restores the same file.
cp /etc/os-release /tmp/os-release.real
printf 'PRETTY_NAME="Debian GNU/Linux 12 (bookworm)"\nID=debian\nVERSION_ID="12"\n' >/etc/os-release
out=$(cb install mtools); code=$?
cp /tmp/os-release.real /etc/os-release
expect "unsupported message" grep -q "catberry install supports Arch-based distros, Fedora, Debian 13 and Ubuntu 26.04 or newer. This system (Debian GNU/Linux 12 (bookworm)) is not one of them." <<<"$out"
expect "unsupported exits 1" test "$code" = 1

echo "== $target: $pass passed, $fail failed"
[ "$fail" = 0 ]
