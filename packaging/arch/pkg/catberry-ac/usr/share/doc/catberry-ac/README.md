# catberry ac

The catberry **autoclicker** (version 1.1.2), for Linux and Windows 10/11.
It clicks, scrolls or presses a keyboard key at a rate you choose.

This branch holds only the autoclicker. The `catberry` command it runs
under lives on the [`main` branch](https://github.com/CatDesuFR/catberry), and
the macro recorder on the [`mtools` branch](https://github.com/CatDesuFR/catberry/tree/mtools).
**Install the `catberry` launcher first**, then this.

## Install

All downloads are in [`release/v1.1.2`](release/v1.1.2).

### CachyOS, Garuda and other Arch-based distros

Installs the launcher and the autoclicker together in one command:

```bash
curl -LO https://github.com/CatDesuFR/catberry/raw/main/release/v1.1.0/catberry-1.1.0-1-x86_64.pkg.tar.zst && curl -LO https://github.com/CatDesuFR/catberry/raw/ac/release/v1.1.2/catberry-ac-1.1.2-1-x86_64.pkg.tar.zst && sudo pacman -U catberry-1.1.0-1-x86_64.pkg.tar.zst catberry-ac-1.1.2-1-x86_64.pkg.tar.zst
```

Remove it with `sudo pacman -R catberry-ac`.

### Other Linux distros

Download [`catberry-ac-linux-x86_64`](https://github.com/CatDesuFR/catberry/raw/ac/release/v1.1.2/catberry-ac-linux-x86_64)
(needs glibc 2.38 or newer: Fedora 39+, Ubuntu 24.04+, Debian 13+) and put
it next to the `catberry` launcher:

```bash
install -Dm755 ~/Downloads/catberry-ac-linux-x86_64 ~/.local/bin/catberry-ac
```

Or build it: `make install` on this branch (needs `g++`, `make` and the
libX11/libXtst development packages).

### Windows

Download [`catberry-ac-windows.zip`](https://github.com/CatDesuFR/catberry/raw/ac/release/v1.1.2/catberry-ac-windows.zip),
extract it and double-click `install.bat`. It goes into the same folder as
the launcher (`%LOCALAPPDATA%\Programs\catberry`). `uninstall.bat` removes it.

### Global hotkey on Linux (once)

For the start/stop hotkey to work while other windows are focused, add
yourself to the `input` group, then log out and back in:

```bash
sudo usermod -aG input $USER
```

Not needed on Windows or on an X11 session.

## Use

```
catberry ac gui     open the autoclicker
catberry ac -v      print the version
catberry ac -help   show the help
```

- **Clicks per second**: type a number (0.1 to 1000) or use `-` / `+`.
- **Click with**: Left, Middle or Right click, Scroll up or down, or
  **Key...**: click it, then press any keyboard key (for example `E` or
  `Space`), and the autoclicker presses that key instead of clicking.
- **Start/stop hotkey**: default F6. Click the button and press a new key to
  change it. It cannot be the same key as the pressed key.
- **Start** waits 3 seconds so you can move the mouse away; the hotkey starts
  straight away. While it runs, clicking anywhere on its window stops it.
- **Click test**: start the clicker and hover over the box to see how many
  clicks per second actually arrive.

Settings are saved in `~/.config/catberry/ac.conf` (Linux) or
`%APPDATA%\catberry\ac.conf` (Windows).

Autoclickers are against the rules of many online games, and catberry does
nothing to hide itself. Use it at your own risk.

## Build

| Target | Command | Output |
| --- | --- | --- |
| Linux program | `make` | `catberry-ac` |
| Install on Linux | `make install` | `~/.local/bin/catberry-ac` |
| Windows package | `make windows` | `dist/catberry-ac-windows.zip` |
| Arch package | `cd packaging/arch && makepkg` | `catberry-ac-1.1.2-1-x86_64.pkg.tar.zst` |

`src/common` is the window logic and clicker, `src/linux` and `src/windows`
the platform parts. The Windows package is cross-compiled with MinGW-w64.
