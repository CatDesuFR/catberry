#!/bin/bash
# Installs a built package in a fresh container of its distro, with only
# what the package manager pulls in, and checks `<program> -v`.
#   PROGRAM=catberry-ac VERSION=1.1.2 ci/smoke.sh <target> <package dir> [launcher dir]
# The launcher dir holds the launcher's packages, for tools that need it.
set -euo pipefail
target=$1
pkgdir=$(realpath "$2")
launcherdir=${3:+$(realpath "$3")}
: "${PROGRAM:?}" "${VERSION:?}"

case "$target" in
arch) pattern='*.pkg.tar.zst' ;;
fedora) pattern='*.rpm' ;;
debian13 | ubuntu26.04) pattern="*+${target}_amd64.deb" ;;
*) echo "unknown target $target" >&2; exit 2 ;;
esac

# apt with retries: Ubuntu's mirrors sometimes serve a file mid-update
# ("Hash Sum mismatch"); a fresh try usually gets the right one.
apt_install() {
    for try in 1 2 3; do
        apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y -o Acquire::Retries=3 "$@" && return 0
        echo "apt failed (try $try), retrying" >&2
        rm -rf /var/lib/apt/lists/partial/* /var/cache/apt/archives/partial/*
        sleep 10
    done
    return 1
}

shopt -s nullglob
files=("$pkgdir"/$pattern)
[ -n "$launcherdir" ] && files+=("$launcherdir"/$pattern)
[ ${#files[@]} -gt 0 ] || { echo "no $pattern in $pkgdir" >&2; exit 1; }

# Only hard dependencies (no recommended/weak packages), so a missing
# Depends shows up here instead of on someone's desktop.
case "$target" in
arch) pacman -Syu --noconfirm && pacman -U --noconfirm "${files[@]}" ;;
fedora) dnf install -y --setopt=install_weak_deps=False "${files[@]}" ;;
*) apt_install --no-install-recommends "${files[@]}" ;;
esac

# roofce loads these Qt plugins at runtime; no library link pulls them in.
if [ "$PROGRAM" = catberry-roofce ]; then
    plugin() { find /usr/lib /usr/lib64 -path '*qt6/plugins/*' -name "$1" 2>/dev/null | grep -q .; }
    missing=()
    plugin liblayer-shell.so || missing+=("layer-shell (wayland-shell-integration)")
    plugin libqwayland.so || plugin libqwayland-generic.so || missing+=("Wayland platform")
    plugin libffmpegmediaplugin.so || missing+=("FFmpeg media backend")
    if [ ${#missing[@]} -gt 0 ]; then
        echo "Qt plugins missing after install: ${missing[*]}" >&2
        exit 1
    fi
fi

out=$(QT_QPA_PLATFORM=offscreen "$PROGRAM" -v 2>&1)
echo "$out"
grep -q "$VERSION" <<<"$out" || { echo "$PROGRAM -v does not print $VERSION" >&2; exit 1; }
echo "smoke $target: OK"
