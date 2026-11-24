#!/bin/sh
# Builds the mod for GD 2.2081 (Geode 5.11.2) next to the main build (GD 2.2091,
# Geode 6): ../cats-mod-menu-2081 shares this source and has its own mod.json.
# Needs the SDK at $GEODE_SDK-5 (git worktree of geode-sdk at v5.11.2) with
# its binaries: GEODE_SDK=$GEODE_SDK-5 geode sdk install-binaries -p win -v 5.11.2
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
out="$here/../cats-mod-menu-2081"
sdk="${GEODE_SDK:-$HOME/.local/share/geode-sdk}-5"
mkdir -p "$out"
for f in src resources CMakeLists.txt about.md changelog.md README.md; do ln -sfn "../cats-mod-menu/$f" "$out/$f"; done
sed 's/"geode": "[^"]*"/"geode": "5.11.2"/; s/"win": "2.2091"/"win": "2.2081"/' "$here/mod.json" > "$out/mod.json"
if [ ! -f "$out/build-win/CMakeCache.txt" ]; then
    GEODE_SDK="$sdk" cmake -S "$out" -B "$out/build-win" -DHOST_ARCH=x64 -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_TOOLCHAIN_FILE="$HOME/.local/share/Geode/cross-tools/clang-msvc-sdk/clang-msvc.cmake" \
        -DSPLAT_DIR="$HOME/.local/share/Geode/cross-tools/splat" -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
        -DGEODE_DONT_INSTALL_MODS=ON -DCMM_NO_DEBUG=ON -DGEODE_CLI="$HOME/.local/opt/geode-cli/geode"
fi
GEODE_SDK="$sdk" cmake --build "$out/build-win" -j"$(nproc)"
echo "Built $out/build-win/catdesu.cats-mod-menu.geode (GD 2.2081)"
