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
out=$(cb ac install --reinstall); code=$?
expect "--reinstall exits 0" test "$code" = 0
expect "--reinstall installs" grep -q "ac 1.1.2 installed" <<<"$out"

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

if [ "${TEST_UNINSTALL:-}" = yes ]; then
    # Uninstall scenarios (Task 7) are appended here.
    :
fi

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
