#!/bin/sh
# Builds the launcher and installs its files under the root given, the same
# files the Arch package has. Used by the Fedora and Debian packages:
#   packaging/stage.sh <root>      (run from the top of this branch)
set -eu
root=$1
make catberry
install -Dm755 catberry "$root/usr/bin/catberry"
# Shared by the tools: lets them create their virtual mouse/keyboard.
install -Dm644 packaging/arch/70-catberry-uinput.rules "$root/usr/lib/udev/rules.d/70-catberry-uinput.rules"
install -Dm644 README.md "$root/usr/share/doc/catberry/README.md"
