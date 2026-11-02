# catberry mtools

The catberry **macro recorder** and **background hotkey macros** (version
1.1.0), for Linux and Windows 10/11.

This branch holds only mtools. The `catberry` command it runs under lives on
the [`main` branch](https://github.com/CatDesuFR/catberry), and the
autoclicker on the [`ac` branch](https://github.com/CatDesuFR/catberry/tree/ac).
**Install the `catberry` launcher first**, then this.

## Install

All downloads are in [`release/v1.1.0`](release/v1.1.0).

### CachyOS, Garuda and other Arch-based distros

Installs the launcher and mtools together in one command:

```bash
curl -LO https://github.com/CatDesuFR/catberry/raw/main/release/v1.1.0/catberry-1.1.0-1-x86_64.pkg.tar.zst && curl -LO https://github.com/CatDesuFR/catberry/raw/mtools/release/v1.1.0/catberry-mtools-1.1.0-1-x86_64.pkg.tar.zst && sudo pacman -U catberry-1.1.0-1-x86_64.pkg.tar.zst catberry-mtools-1.1.0-1-x86_64.pkg.tar.zst
```

Remove it with `catberry -configunload` and then `sudo pacman -R catberry-mtools`.

### Other Linux distros

Download [`catberry-mtools-linux-x86_64`](https://github.com/CatDesuFR/catberry/raw/mtools/release/v1.1.0/catberry-mtools-linux-x86_64)
(needs glibc 2.38 or newer: Fedora 39+, Ubuntu 24.04+, Debian 13+) and put
it next to the `catberry` launcher:

```bash
install -Dm755 ~/Downloads/catberry-mtools-linux-x86_64 ~/.local/bin/catberry-mtools
```

Or build it: `make install` on this branch (needs `g++`, `make` and the
libX11/libXtst development packages).

### Windows

Download [`catberry-mtools-windows.zip`](https://github.com/CatDesuFR/catberry/raw/mtools/release/v1.1.0/catberry-mtools-windows.zip),
extract it and double-click `install.bat`. It goes into the same folder as
the launcher (`%LOCALAPPDATA%\Programs\catberry`). `uninstall.bat` removes it.

### Linux permission (once)

Recording, the hotkeys and `-configload` read the keyboard directly, which
needs the `input` group. Run this, then log out and back in:

```bash
sudo usermod -aG input $USER
```

Not needed on Windows.

## Use

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

- **Record** / **F8**: start and stop recording.
- **Play** / **F9**: play it back. **Repeat** sets how many times (0 = until
  stopped). The Play button waits 3 seconds first.
- Type a name and press **Save** or **Load** to keep macros.

### Hotkey macros

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

Settings, macros and configs are saved in `~/.config/catberry/` (Linux) or
`%APPDATA%\catberry\` (Windows), in the same format on both.

Macros are against the rules of many online games, and catberry does nothing
to hide itself. Use it at your own risk.

## Build

| Target | Command | Output |
| --- | --- | --- |
| Linux program | `make` | `catberry-mtools` |
| Install on Linux | `make install` | `~/.local/bin/catberry-mtools` |
| Windows package | `make windows` | `dist/catberry-mtools-windows.zip` |
| Arch package | `cd packaging/arch && makepkg` | `catberry-mtools-1.1.0-1-x86_64.pkg.tar.zst` |

`src/common` is the window logic, recorder and configs, `src/linux` and
`src/windows` the platform parts. The Windows package is cross-compiled with
MinGW-w64.
