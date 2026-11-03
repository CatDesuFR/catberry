#!/bin/sh
# Builds catberry-ac and installs its files under the root given, the same files
# the Arch package has. Used by the Fedora and Debian packages:
#   packaging/stage.sh <root>      (run from the top of this branch)
set -eu
root=$1
make catberry-ac
install -Dm755 catberry-ac "$root/usr/bin/catberry-ac"
install -Dm644 data/catberry-ac.desktop "$root/usr/share/applications/catberry-ac.desktop"
install -Dm644 README.md "$root/usr/share/doc/catberry-ac/README.md"
