#!/bin/bash
# Runs ci/test-install.sh on every distro with podman: builds the launcher
# package from this tree, serves a local copy of the published releases
# (and a damaged copy) over HTTP, and points catberry at it with
# CATBERRY_RELEASES.
#   ci/test-install-local.sh [target...]      (default: all four)
set -euo pipefail
repo=CatDesuFR/catberry
ci=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$ci")
targets=("$@")
[ ${#targets[@]} -gt 0 ] || targets=(arch fedora debian13 ubuntu26.04)
work=${WORK:-$(mktemp -d "${TMPDIR:-/tmp}/catberry-test.XXXXXX")}
echo "== working in $work"

image_of() {
    case "$1" in
    arch) echo docker.io/library/archlinux:latest ;;
    fedora) echo registry.fedoraproject.org/fedora:latest ;;
    debian13) echo docker.io/library/debian:13 ;;
    ubuntu26.04) echo docker.io/library/ubuntu:26.04 ;;
    esac
}

# A copy of the releases: releases.json in GitHub's format, <tag>/<files>.
mirror="$work/mirror"
if [ ! -f "$mirror/releases.json" ]; then
    mkdir -p "$mirror"
    tags=$(gh release list -R "$repo" --limit 200 --json tagName -q '.[].tagName')
    {
        echo "["
        first=1
        for tag in $tags; do
            [ $first = 1 ] || echo ","
            first=0
            printf '  {\n    "tag_name": "%s"\n  }' "$tag"
        done
        echo
        echo "]"
    } >"$mirror/releases.json"
    for tag in $tags; do
        case "$tag" in *-v*) ;; *) continue ;; esac
        gh release download "$tag" -R "$repo" -D "$mirror/$tag" --clobber
    done
    # The same releases with every package changed after its hash was taken.
    mkdir -p "$mirror/damaged"
    cp "$mirror/releases.json" "$mirror/damaged/"
    for dir in "$mirror"/*-v*; do
        d="$mirror/damaged/$(basename "$dir")"
        mkdir -p "$d"
        cp "$dir"/* "$d/"
        for f in "$d"/*; do [ "$(basename "$f")" = SHA256SUMS ] || echo damaged >>"$f"; done
    done
fi

port=8765
python3 -m http.server "$port" --bind 0.0.0.0 --directory "$mirror" >"$work/http.log" 2>&1 &
server=$!
trap 'kill $server 2>/dev/null' EXIT
sleep 1

version=$(sed -n 's/^pkgver=//p' "$top/packaging/arch/PKGBUILD")
failed=()
for target in "${targets[@]}"; do
    echo "== $target: build the launcher package"
    src="$work/src-$target" out="$work/pkg-$target"
    mkdir -p "$src" "$out"
    (cd "$top" && git ls-files -co --exclude-standard -z | xargs -0 tar -c) | tar -x -C "$src"
    podman run --rm -v "$src:/src:Z" -v "$ci:/ci:ro,Z" -v "$out:/out:Z" -w /src \
        -e TOOL=catberry -e PROGRAM=catberry -e VERSION="$version" -e REL=1 \
        "$(image_of "$target")" bash /ci/build.sh "$target" /out >"$work/build-$target.log" 2>&1 ||
        { echo "build failed: $work/build-$target.log"; failed+=("$target"); continue; }
    echo "== $target: test"
    podman run --rm -v "$ci:/ci:ro,Z" -v "$out:/pkg:ro,Z" \
        -e CATBERRY_RELEASES="http://host.containers.internal:$port" \
        "$(image_of "$target")" bash /ci/test-install.sh "$target" /pkg 2>&1 | tee "$work/test-$target.log" |
        grep -E "^(ok|FAIL|==)" || true
    tail -1 "$work/test-$target.log" | grep -q " 0 failed" || failed+=("$target")
done
if [ ${#failed[@]} -gt 0 ]; then
    echo "FAILED: ${failed[*]}"
    exit 1
fi
echo "ALL PASSED: ${targets[*]}"
