#!/bin/bash
# Builds one package of one tool. Runs inside a fresh container of the
# target distro, as root, from the top of the tool's source tree:
#   TOOL=ac PROGRAM=catberry-ac VERSION=1.1.2 REL=1 ci/build.sh <target> <out dir>
# Targets: arch, fedora, debian13, ubuntu26.04, windows.
# Used by ci/release-local.sh and .github/workflows/release.yml.
set -euo pipefail
target=$1
out=$(realpath -m "$2")
: "${TOOL:?}" "${PROGRAM:?}" "${VERSION:?}" "${REL:?}"
mkdir -p "$out"

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

case "$target" in
arch)
    case "$TOOL" in
    ac | mtools) extra="libx11 libxtst" ;;
    roofce) extra="cmake qt6-base qt6-multimedia layer-shell-qt" ;;
    *) extra="" ;;
    esac
    pacman -Syu --noconfirm --needed base-devel $extra
    # makepkg refuses to run as root. Dependencies are installed above, so
    # --nodeps (ac and mtools depend on the catberry package, not in Arch's repos).
    id builder >/dev/null 2>&1 || useradd -m builder
    chown -R builder: .
    su builder -c 'cd packaging/arch && makepkg --nodeps --noconfirm --force'
    cp packaging/arch/*.pkg.tar.zst "$out/"
    ;;
fedora)
    dnf install -y rpm-build dnf-plugins-core
    spec="packaging/fedora/$PROGRAM.spec"
    defs=(--define "pkgver $VERSION" --define "pkgrel $REL")
    dnf builddep -y "${defs[@]}" "$spec"
    rpmbuild -bb --build-in-place "${defs[@]}" --define "debug_package %{nil}" \
        --define "_topdir $PWD/rpmbuild" "$spec"
    cp rpmbuild/RPMS/x86_64/*.rpm "$out/"
    ;;
debian13 | ubuntu26.04)
    case "$TOOL" in
    ac | mtools) extra="libx11-dev libxtst-dev" ;;
    roofce) extra="cmake qt6-base-dev qt6-multimedia-dev liblayershellqtinterface-dev" ;;
    *) extra="" ;;
    esac
    apt_install --no-install-recommends build-essential dpkg-dev $extra
    debver="$VERSION-$REL+$target"
    rm -rf root shlibs
    packaging/stage.sh "$PWD/root"
    # Library dependencies, in this distro's package names.
    mkdir -p shlibs/debian && echo "Source: $PROGRAM" >shlibs/debian/control
    shlibs=$(cd shlibs && dpkg-shlibdeps -O "../root/usr/bin/$PROGRAM" | sed 's/^shlibs:Depends=//')
    mkdir -p root/DEBIAN
    sed -e "s/@VERSION@/$debver/" -e "s/@SHLIBS@/$shlibs/" packaging/debian/control >root/DEBIAN/control
    if [ -f packaging/debian/postinst ]; then install -m755 packaging/debian/postinst root/DEBIAN/postinst; fi
    dpkg-deb --build --root-owner-group root "$out/${PROGRAM}_${debver}_amd64.deb"
    ;;
windows)
    apt_install --no-install-recommends g++-mingw-w64-x86-64 make zip
    make windows WINCXX=x86_64-w64-mingw32-g++-posix
    cp "dist/$PROGRAM-windows.zip" "$out/"
    ;;
*)
    echo "unknown target $target" >&2
    exit 2
    ;;
esac
ls -l "$out"
