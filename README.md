# catberry

`catberry` is one command for a set of input tools, on **Linux** and
**Windows 10/11**. This branch is the `catberry` command itself (version
1.1.0). Each tool lives on its own branch and is installed separately:

| Tool | Branch | Version | What it does |
| --- | --- | --- | --- |
| `catberry ac` | [`ac`](https://github.com/CatDesuFR/catberry/tree/ac) | 1.1.2 | Autoclicker: clicks, scrolls or presses a key at a set rate |
| `catberry mtools` | [`mtools`](https://github.com/CatDesuFR/catberry/tree/mtools) | 1.1.0 | Macro recorder, plus hotkey macros (`catberry -config`) |

Install this launcher first, then the tools you want.

## Install

### CachyOS, Garuda and other Arch-based distros

Everything (launcher, autoclicker and mtools) in one command:

```bash
curl -LO https://github.com/CatDesuFR/catberry/raw/main/release/v1.1.0/catberry-1.1.0-1-x86_64.pkg.tar.zst \
     -LO https://github.com/CatDesuFR/catberry/raw/ac/release/v1.1.2/catberry-ac-1.1.2-1-x86_64.pkg.tar.zst \
     -LO https://github.com/CatDesuFR/catberry/raw/mtools/release/v1.1.0/catberry-mtools-1.1.0-1-x86_64.pkg.tar.zst \
  && sudo pacman -U catberry-1.1.0-1-x86_64.pkg.tar.zst catberry-ac-1.1.2-1-x86_64.pkg.tar.zst catberry-mtools-1.1.0-1-x86_64.pkg.tar.zst
```

Leave out a tool's file to skip it. This also replaces the older all-in-one
`catberry` 1.0.2 package. Remove everything with
`sudo pacman -R catberry-ac catberry-mtools catberry`.

### Other Linux distros

Download [`catberry-linux-x86_64`](https://github.com/CatDesuFR/catberry/raw/main/release/v1.1.0/catberry-linux-x86_64)
(needs glibc 2.38 or newer: Fedora 39+, Ubuntu 24.04+, Debian 13+) and
install it as `catberry`:

```bash
install -Dm755 ~/Downloads/catberry-linux-x86_64 ~/.local/bin/catberry
```

Then install the tools from their branches into the same folder. Or build
it: `make install` on this branch.

### Windows

Download [`catberry-windows.zip`](https://github.com/CatDesuFR/catberry/raw/main/release/v1.1.0/catberry-windows.zip),
extract it and double-click `install.bat` (if Windows shows "Windows protected
your PC", click **More info** → **Run anyway**). Then do the same with
`catberry-ac-windows.zip` and `catberry-mtools-windows.zip` from the tool
branches. All of them go into `%LOCALAPPDATA%\Programs\catberry`, which is
added to your PATH; open a new terminal afterwards.

### Linux permission (once)

The hotkeys and macro recording read the keyboard directly. Run this, then
log out and back in:

```bash
sudo usermod -aG input $USER
```

Not needed on Windows.

## Use

```
catberry -help                  show the help
catberry -v                     print the versions of catberry and its tools

catberry ac gui                 open the autoclicker
catberry mtools gui             open the macro recorder
catberry -config NAME KEYS HOTKEY COUNT
                                save a hotkey macro, eg: -config EasyEdit E,LMB F6 1
catberry -configload NAME       start a saved macro in the background
catberry -configunload [NAME]   stop one loaded macro, or all of them
```

`catberry -v` shows which tools are installed:

```
catberry 1.1.0
  ac      1.1.2
  mtools  1.1.0
```

`catberry ac ...` runs the `catberry-ac` program and `catberry mtools ...` (and
the `-config` commands) run `catberry-mtools`. The launcher looks for them
next to itself first, then on your PATH. See each branch's README for how the
tools work.

## Build

| Target | Command | Output |
| --- | --- | --- |
| Linux program | `make` | `catberry` |
| Install on Linux | `make install` | `~/.local/bin/catberry` |
| Windows package | `make windows` | `dist/catberry-windows.zip` |
| Arch package | `cd packaging/arch && makepkg` | `catberry-1.1.0-1-x86_64.pkg.tar.zst` |

Ready-built downloads are in [`release/v1.1.0`](release/v1.1.0), with a
`SHA256SUMS` file to check them against. The Windows package is
cross-compiled with MinGW-w64.
