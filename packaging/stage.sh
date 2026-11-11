#!/bin/sh
# Builds catberry-cbmm and installs its files under the root given, the same files
# the Arch package has. Used by the Fedora and Debian packages:
#   packaging/stage.sh <root>      (run from the top of this branch)
set -eu
root=$1
make catberry-cbmm
install -Dm755 catberry-cbmm "$root/usr/bin/catberry-cbmm"
install -Dm644 README.md "$root/usr/share/doc/catberry-cbmm/README.md"
