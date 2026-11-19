#!/bin/sh
# Builds vskm's Vencord: Vencord (at the commit in VENCORD_COMMIT) with the
# plugins in plugins/ built in, and copies the files Vesktop loads into
# vencord/. Needs git, Node.js and npm (pnpm runs through npx).
#   scripts/build-vencord.sh            (run from the top of this branch)
set -eu
top=$(pwd)
commit=$(cat VENCORD_COMMIT)
work=${VSKM_WORK:-$HOME/.cache/vskm-build}
mkdir -p "$work"
if [ ! -d "$work/Vencord/.git" ]; then
    git clone -q https://github.com/Vendicated/Vencord.git "$work/Vencord"
fi
cd "$work/Vencord"
git fetch -q origin
git checkout -q --force "$commit"
git clean -qfdx src/userplugins 2>/dev/null || true
mkdir -p src/userplugins
for plugin in "$top"/plugins/*/; do
    cp -r "$plugin" "src/userplugins/$(basename "$plugin")"
done
pnpm="npx --yes pnpm@$(sed -n 's/.*"packageManager": "pnpm@\([^"]*\)".*/\1/p' package.json)"
$pnpm install --frozen-lockfile >/dev/null
$pnpm build >/dev/null
rm -rf "$top/vencord"
mkdir -p "$top/vencord"
for f in vencordDesktopMain.js vencordDesktopPreload.js vencordDesktopRenderer.js vencordDesktopRenderer.css; do
    cp "dist/$f" "$top/vencord/$f"
done
cp LICENSE "$top/vencord/LICENSE"
printf 'Vencord %s (https://github.com/Vendicated/Vencord) with the vskm plugins built in.\nSource: Vencord at that commit plus plugins/ on the vskm branch.\n' "$commit" > "$top/vencord/SOURCE"
echo "Built vencord/ from Vencord $commit"
