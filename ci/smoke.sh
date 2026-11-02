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

shopt -s nullglob
files=("$pkgdir"/$pattern)
[ -n "$launcherdir" ] && files+=("$launcherdir"/$pattern)
[ ${#files[@]} -gt 0 ] || { echo "no $pattern in $pkgdir" >&2; exit 1; }

case "$target" in
arch) pacman -Syu --noconfirm && pacman -U --noconfirm "${files[@]}" ;;
fedora) dnf install -y "${files[@]}" ;;
*) apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y "${files[@]}" ;;
esac

out=$(QT_QPA_PLATFORM=offscreen "$PROGRAM" -v 2>&1)
echo "$out"
grep -q "$VERSION" <<<"$out" || { echo "$PROGRAM -v does not print $VERSION" >&2; exit 1; }
echo "smoke $target: OK"
