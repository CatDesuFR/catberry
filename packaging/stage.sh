#!/bin/sh
# Builds catberry-mtools and installs its files under the root given, the same files
# the Arch package has. Used by the Fedora and Debian packages:
#   packaging/stage.sh <root>      (run from the top of this branch)
set -eu
root=$1
make catberry-mtools
install -Dm755 catberry-mtools "$root/usr/bin/catberry-mtools"
install -Dm644 data/catberry-mtools.desktop "$root/usr/share/applications/catberry-mtools.desktop"
install -Dm644 README.md "$root/usr/share/doc/catberry-mtools/README.md"
