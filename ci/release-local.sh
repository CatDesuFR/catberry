#!/bin/bash
# Builds, tests and publishes one version of one catberry tool as a GitHub
# Release, using podman containers on this PC. Same steps as
# .github/workflows/release.yml (which needs GitHub Actions):
#   ci/release-local.sh <catberry|ac|mtools|roofce> [--no-publish]
# The version comes from the tool branch's packaging/arch/PKGBUILD (as on
# GitHub). A version that is already released is never rebuilt or overwritten.
set -euo pipefail
tool=${1:?usage: ci/release-local.sh <catberry|ac|mtools|roofce> [--no-publish]}
publish=yes
[ "${2:-}" = --no-publish ] && publish=no

case "$tool" in
catberry) branch=main program=catberry ;;
ac) branch=ac program=catberry-ac ;;
mtools) branch=mtools program=catberry-mtools ;;
roofce) branch=roof-carton-engine program=catberry-roofce ;;
*) echo "unknown tool $tool" >&2; exit 2 ;;
esac

repo=CatDesuFR/catberry
ci=$(cd "$(dirname "$0")" && pwd)
git fetch -q origin "$branch"
sha=$(git rev-parse "origin/$branch")
pkgbuild=$(git show "$sha:packaging/arch/PKGBUILD")
version=$(sed -n 's/^pkgver=//p' <<<"$pkgbuild")
rel=$(sed -n 's/^pkgrel=//p' <<<"$pkgbuild")
tag="$tool-v$version"
[ -n "$version" ] && [ -n "$rel" ] || { echo "no pkgver/pkgrel in the PKGBUILD" >&2; exit 1; }
if gh release view "$tag" -R "$repo" >/dev/null 2>&1; then
    echo "$tag is already released" >&2
    exit 1
fi
# A tag without a release (versions tagged before releases existed): the
# release goes on that tag, built from the branch as it is now (only it has
# the packaging files), as long as the program itself has not changed.
existing_tag=no
if [ -n "$(git ls-remote --tags origin "refs/tags/$tag")" ]; then
    git fetch -q origin "refs/tags/$tag:refs/tags/$tag"
    if ! git diff --quiet "$tag" "$sha" -- src Makefile CMakeLists.txt data; then
        echo "$tag exists and the program changed since then: raise pkgver in packaging/arch/PKGBUILD" >&2
        exit 1
    fi
    existing_tag=yes
fi
echo "== $tool $version-$rel from $branch ($sha)"

work=${WORK:-$(mktemp -d "${TMPDIR:-/tmp}/catberry-release.XXXXXX")}
mkdir -p "$work/files" "$work/logs"
echo "== working in $work"

# The tools are tested with the launcher installed: its newest release.
launcher=""
if [ "$tool" = ac ] || [ "$tool" = mtools ]; then
    ltag=$(gh release list -R "$repo" --limit 200 --json tagName -q '.[].tagName' | grep '^catberry-v' | sort -V | tail -1 || true)
    [ -n "$ltag" ] || { echo "no catberry-v* release yet: publish catberry first" >&2; exit 1; }
    mkdir -p "$work/launcher"
    gh release download "$ltag" -R "$repo" -D "$work/launcher" --clobber -p '*.pkg.tar.zst' -p '*.rpm' -p '*.deb'
    launcher=/launcher
fi

image_of() {
    case "$1" in
    arch) echo docker.io/library/archlinux:latest ;;
    fedora) echo registry.fedoraproject.org/fedora:latest ;;
    debian13) echo docker.io/library/debian:13 ;;
    ubuntu26.04 | windows) echo docker.io/library/ubuntu:26.04 ;;
    esac
}

targets=(arch fedora debian13 ubuntu26.04)
[ "$tool" != roofce ] && targets+=(windows)

for target in "${targets[@]}"; do
    echo "== build $target"
    src="$work/src-$target"
    # Files the Arch build's container user made belong to a sub-UID here.
    rm -rf "$src" 2>/dev/null || podman unshare rm -rf "$src"
    mkdir -p "$src" "$work/out-$target"
    git archive "$sha" | tar -x -C "$src"
    podman run --rm -v "$src:/src:Z" -v "$ci:/ci:ro,Z" -v "$work/out-$target:/out:Z" -w /src \
        -e TOOL="$tool" -e PROGRAM="$program" -e VERSION="$version" -e REL="$rel" \
        "$(image_of "$target")" bash /ci/build.sh "$target" /out >"$work/logs/build-$target.log" 2>&1 ||
        { echo "build $target failed; see $work/logs/build-$target.log" >&2; tail -20 "$work/logs/build-$target.log" >&2; exit 1; }
done

for target in "${targets[@]}"; do
    [ "$target" = windows ] && continue
    echo "== smoke $target"
    mounts=(-v "$work/out-$target:/pkg:ro,Z" -v "$ci:/ci:ro,Z")
    [ -n "$launcher" ] && mounts+=(-v "$work/launcher:/launcher:ro,Z")
    podman run --rm "${mounts[@]}" -e PROGRAM="$program" -e VERSION="$version" \
        "$(image_of "$target")" bash /ci/smoke.sh "$target" /pkg $launcher >"$work/logs/smoke-$target.log" 2>&1 ||
        { echo "smoke $target failed; see $work/logs/smoke-$target.log" >&2; tail -20 "$work/logs/smoke-$target.log" >&2; exit 1; }
    tail -1 "$work/logs/smoke-$target.log"
done

for target in "${targets[@]}"; do cp "$work/out-$target"/* "$work/files/"; done
(cd "$work/files" && sha256sum -- * >SHA256SUMS && cat SHA256SUMS)

if [ "$publish" = no ]; then
    echo "== not published (--no-publish); files in $work/files"
    exit 0
fi
echo "== publish $tag"
if [ "$existing_tag" = yes ]; then target=(--verify-tag); else target=(--target "$sha"); fi
(cd "$work/files" && gh release create "$tag" -R "$repo" "${target[@]}" \
    --title "$tool $version" --notes "$program $version" --latest=false -- *)
