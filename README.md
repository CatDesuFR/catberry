# catberry mtools

The catberry **macro recorder**, **background hotkey macros** and **quick click** (version
2.1.2), for Linux and Windows 10/11.

This branch holds only mtools. The `catberry` command it runs under lives on
the [`main` branch](https://github.com/CatDesuFR/catberry), and the
autoclicker on the [`ac` branch](https://github.com/CatDesuFR/catberry/tree/ac).
**Install the `catberry` launcher first**, then this.

## Install

Install the [`catberry` launcher](https://github.com/CatDesuFR/catberry#install)
first, then:

```bash
catberry install mtools
```

It picks the right package for your system (Arch-based distros, Fedora,
Debian 13, Ubuntu 26.04 or newer, Windows 10/11) and installs it. Other versions:
`catberry versions mtools`, then `catberry install mtools <version>`. Remove it with
`catberry uninstall mtools` (it asks whether to keep your settings). Every
download is on the [Releases](https://github.com/CatDesuFR/catberry/releases)
page (tags `mtools-v...`).

### Build it yourself

`make install` on this branch (needs `g++`, `cmake` and Qt 6: `qt6-base` on
Arch, `qt6-qtbase-devel` on Fedora, `qt6-base-dev` on Debian/Ubuntu), or on
Arch-based distros `cd packaging/arch && makepkg -si`.

### Linux permission (once)

Recording, the hotkeys and `-configload` read the keyboard directly, which
needs the `input` group. Run this, then log out and back in:

```bash
sudo usermod -aG input $USER
```

Not needed on Windows. Until then the window shows this command, and
recording is off. Playback goes through `/dev/uinput`, which the `catberry`
launcher package allows.

## Use

Open **Catberry Macro Recorder** from the app menu (on Linux a native
Wayland window, Qt 6), or use the commands:

```
catberry mtools gui             open the macro recorder
catberry mtools -v              print the version
catberry mtools -help           show the help
catberry -config NAME KEYS HOTKEY COUNT
                                save a hotkey macro
catberry -configload NAME       start a saved macro in the background
catberry -configunload [NAME]   stop one loaded macro, or all of them
```

### Macro recorder

Records keyboard keys, mouse buttons and the scroll wheel with their timing,
then plays them back (mouse movement is not recorded).

The **Recorder** tab:

- **Record** / **F8**: start and stop recording.
- **Play** / **F9**: play it back. **Repeat** sets how many times (0 = until
  stopped). The Play button waits 3 seconds first; clicking the window stops
  playback. Click a hotkey button and press a key to change it.
- **Saved macros**: every saved macro with its length. Click one to load it;
  **Save as...**, **Rename...** and **Delete** manage them. The last one is
  loaded again next time.

### Hotkey macros

A hotkey macro presses a list of keys every time you press its hotkey, in
the background, also after the window is closed. The **Hotkey macros** tab
lists them with their status: **New...** / **Edit...** (the keys are checked
as you type; the hotkey can be a key or a side mouse button), **Delete**,
**Load**, **Unload** and **Unload all**. The same from the terminal:

```bash
catberry -config EasyEdit E,LMB F6 1   # F6 taps E, then left click, once
catberry -configload EasyEdit          # start it in the background
catberry -configunload EasyEdit        # stop it (no name: stop all)
```

- `KEYS` is a comma-separated list, pressed one after another.
- `COUNT` is how many times the list runs per hotkey press. Pressing the
  hotkey again while it runs cancels it. Several configs can be loaded at once.
- Key names: letters, digits, `F1`-`F12`, `Space`, `Enter`, `Tab`, `Esc`,
  `Backspace`, `Shift`, `Ctrl`, `Alt`, `Super`, `Up`/`Down`/`Left`/`Right`,
  `Insert`, `Delete`, `Home`, `End`, `PageUp`, `PageDown`, `KP0`-`KP9`, and the
  mouse: `LMB`, `RMB`, `MMB`, `MB4`, `MB5`, `ScrollUp`, `ScrollDown`.
- Each config has a `delay_ms` line (default 15) for how long each key is held
  and the pause before the next one. Raise it if a game misses keys.

### Quick click

Pick points on the screen; every press of the hotkey clicks them all, in
order, as fast as the desktop reliably takes it (0.1 ms apart by default:
50 points in under 10 ms). Each point can be a Left, Right, Middle or Double
click. Like hotkey macros, quick clicks run in the background, also after
the window is closed.

The **Quick click** tab lists them with their status: **New...** asks for a
name, a hotkey (a key or a side mouse button) and the gap, then **Pick
points...** covers the screen: click where it should click, **L / R / M / D**
choose the click type, **Backspace** removes the last point, **Enter**
saves, **Esc** cancels. **Edit...** changes a point's click type or order,
or picks them again. Then **Load** / **Unload**. The same from the terminal:

```bash
catberry -quickpick Farm F7 0.1   # pick the points; F7 clicks them, 0.1 ms apart
catberry -quickload Farm          # start it in the background
catberry -quickunload Farm        # stop it (no name: stop all)
```

**For games**: a game reads clicks and redraws its menus once a frame, so
clicks that come faster land on a menu that has not changed yet (for example
a loadout where each click opens the next category). Tick **For a game: wait
for the game between points** and choose the game's frame rate (its FPS, not
the monitor's refresh rate): with 2 frames at 144 FPS the points are 13.9 ms
apart, four points in about 42 ms.

Not sure which gap a game menu needs? **Find the fastest gap...** in the
quick click's Edit dialog tries the points at 5, 10, 15 ... 100 ms: open the
menu, press the hotkey, and press **Enter** when every point came out right
(or open the menu again and press the hotkey for the next, slower try).

On KDE the "Shake cursor" effect (which enlarges the pointer when it moves
fast) is switched off during a run and back on right after. A quick click
needs the same `input` group as the hotkeys.

Settings, macros, configs and quick clicks are saved in `~/.config/catberry/` (Linux) or
`%APPDATA%\catberry\` (Windows), in the same format on both.

Macros are against the rules of many online games, and catberry does nothing
to hide itself. Use it at your own risk.

## Build

| Target | Command | Output |
| --- | --- | --- |
| Linux program | `make` | `catberry-mtools` |
| Install on Linux | `make install` | `~/.local/bin/catberry-mtools` |
| Windows package | `make windows` | `dist/catberry-mtools-windows.zip` |
| Arch package | `cd packaging/arch && makepkg` | `catberry-mtools-2.1.2-1-x86_64.pkg.tar.zst` |
| Unit tests | `make test` | |

On Linux, `make` builds with CMake and Qt 6 (`src/qt` is the window). On
Windows the window is drawn by `src/common/mtools.cpp` and `src/windows`.
`src/common` also holds the recorder, player and configs, shared by both. The
Windows package is cross-compiled with MinGW-w64.
