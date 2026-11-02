#!/bin/sh
# Builds catberry-roofce and installs its files under the root given, the
# same files the Arch package has. Used by the Fedora and Debian packages:
#   packaging/stage.sh <root>      (run from the top of this branch)
set -eu
root=$1
cmake -S . -B build-pkg -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-pkg -j"$(nproc)"
install -Dm755 build-pkg/catberry-roofce "$root/usr/bin/catberry-roofce"
install -Dm644 data/catberry-roofce.desktop "$root/usr/share/applications/catberry-roofce.desktop"
install -Dm644 README.md "$root/usr/share/doc/catberry-roofce/README.md"
