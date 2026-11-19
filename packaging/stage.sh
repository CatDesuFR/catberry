#!/bin/sh
# Builds catberry-vskm and installs its files under the root given, the same files
# the Arch package has. Used by the Fedora and Debian packages:
#   packaging/stage.sh <root>      (run from the top of this branch)
set -eu
root=$1
make catberry-vskm
install -Dm755 catberry-vskm "$root/usr/bin/catberry-vskm"
for f in vencord/*; do
    install -Dm644 "$f" "$root/usr/share/catberry-vskm/vencord/$(basename "$f")"
done
install -Dm644 README.md "$root/usr/share/doc/catberry-vskm/README.md"
