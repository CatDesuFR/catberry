# catberry cbmm — Catberry's Mod Menu

`catberry cbmm` puts **Catberry's Mod Menu** into Geometry Dash. It is a mod
menu for Geometry Dash 2.2091 made with [Geode](https://geode-sdk.org), written
from scratch. This branch has the mod's source (`mod/`) and the small
`catberry-cbmm` program that installs it.

## Install

Install the [catberry launcher](https://github.com/CatDesuFR/catberry) first,
then:

```bash
catberry install cbmm
catberry cbmm load
```

`load` finds Geometry Dash in your Steam libraries (Linux, including Proton,
and Windows) and copies the mod into its `geode/mods` folder. Geode must
already be installed in the game. Restart Geometry Dash, then press **Tab** in
game to open the menu.

The mod is built for Geometry Dash 2.2091 (Geode 6) and 2.2081 (Geode 5).
`load` reads which one you have from Geode's log and puts in the matching
build (start the game with Geode once first); `status` shows what it found.

```
catberry cbmm load      copy the mod into Geometry Dash's geode/mods folder
catberry cbmm unload    remove it from there again (your settings and macros stay)
catberry cbmm status    show Geometry Dash, its Geode version and whether the mod is loaded
catberry cbmm -v        print the version
```

`catberry update cbmm` gets the newest version; run `catberry cbmm load`
again afterwards. `catberry uninstall cbmm` asks whether to also remove the
mod from Geometry Dash.

## What's in the mod

Hacks in the style of MegaHack and Eclipse, each marked with its status on
Pointercrate's list (green = allowed, red = not allowed); a cheat indicator
label turns red while a disallowed hack is on. Highlights:

- **Click Before Frames**: follows a replay you recorded with the Replay Bot.
  A click a few ticks too early is moved onto the first tick that works.
- **Frame Window Analyser / Counter**: works out every replay click's frame
  window and shows it as a ring and number in NaN GD's colours.
- Replay Bot, Show Trajectory, Show Hitboxes (all types, on death, trail),
  Noclip with accuracy limits, Smart StartPos and a StartPos switcher,
  Hitbox Multiplier, Layout Mode, labels (FPS, CPS, attempts, best run, custom
  text with tags...), keybinds and favourites, and many quality-of-life and
  bypass options.

See `mod/changelog.md` for every version.

## Build

| Target | Command | Output |
| --- | --- | --- |
| The mod | in `mod/`: Geode SDK + CLI, `geode build` (Windows target) | `catdesu.cats-mod-menu.geode` |
| Linux program | `make` (uses `geode/catdesu.cats-mod-menu.geode`) | `catberry-cbmm` |
| Windows package | `make windows` | `dist/catberry-cbmm-windows.zip` |
| Arch package | `cd packaging/arch && makepkg` | `catberry-cbmm-<version>-1-x86_64.pkg.tar.zst` |

The compiled mod in `geode/` is built into `catberry-cbmm`, so the program is
one file on every system. Packages and releases are made with the launcher's
`ci/release-local.sh cbmm` on the `main` branch.

The menu font is Roboto (Apache License 2.0).
