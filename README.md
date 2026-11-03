# catberry

`catberry` is one command for a set of input tools, on **Linux** and
**Windows 10/11**. This branch is the `catberry` command itself (version
1.3.3). Each tool lives on its own branch and is installed separately:

| Tool | Branch | Version | What it does |
| --- | --- | --- | --- |
| `catberry ac` | [`ac`](https://github.com/CatDesuFR/catberry/tree/ac) | 2.0.0 | Autoclicker: clicks, scrolls or presses a key at a set rate |
| `catberry mtools` | [`mtools`](https://github.com/CatDesuFR/catberry/tree/mtools) | 2.0.0 | Macro recorder, plus hotkey macros (`catberry -config`) |
| `catberry roofce` | [`roof-carton-engine`](https://github.com/CatDesuFR/catberry/tree/roof-carton-engine) | 1.2.2 | Roof Carton Engine: live wallpapers, clock and music widgets (Linux, Wayland) |

Install this launcher once; it then installs, updates and removes the tools.

## Install

### 1. The launcher

**CachyOS, Garuda and other Arch-based distros**

```bash
curl -LO https://github.com/CatDesuFR/catberry/releases/download/catberry-v1.3.3/catberry-1.3.3-1-x86_64.pkg.tar.zst && sudo pacman -U catberry-1.3.3-1-x86_64.pkg.tar.zst
```

**Fedora**

```bash
sudo dnf install https://github.com/CatDesuFR/catberry/releases/download/catberry-v1.3.3/catberry-1.3.3-1.fc44.x86_64.rpm
```

**Debian 13**

```bash
curl -LO https://github.com/CatDesuFR/catberry/releases/download/catberry-v1.3.3/catberry_1.3.3-1+debian13_amd64.deb && sudo apt install ./catberry_1.3.3-1+debian13_amd64.deb
```

**Ubuntu 26.04 or newer** (and distros built on it, like Linux Mint)

```bash
curl -LO https://github.com/CatDesuFR/catberry/releases/download/catberry-v1.3.3/catberry_1.3.3-1+ubuntu26.04_amd64.deb && sudo apt install ./catberry_1.3.3-1+ubuntu26.04_amd64.deb
```

**Windows 10/11**: download [`catberry-windows.zip`](https://github.com/CatDesuFR/catberry/releases/download/catberry-v1.3.3/catberry-windows.zip),
extract it and double-click `install.bat` (if Windows shows "Windows protected
your PC", click **More info** → **Run anyway**). It goes into
`%LOCALAPPDATA%\Programs\catberry`, which is added to your PATH; open a new
terminal afterwards.

All downloads, for every version: [Releases](https://github.com/CatDesuFR/catberry/releases).

### 2. The tools

```bash
catberry install ac          # the autoclicker
catberry install mtools      # the macro recorder
catberry install roofce      # the wallpaper manager (Linux, Wayland)
```

catberry picks the right package for your system and installs it with
pacman, dnf or apt (it asks for your password with `sudo`), or into the
catberry folder on Windows.

```
catberry install TOOL [VERSION]   install a tool, or another version of it (default: latest)
catberry install TOOL --reinstall install the same version again
catberry install catberry         update the launcher itself
catberry versions TOOL            list the published versions
catberry uninstall TOOL           remove a tool; you choose whether to keep its settings
catberry uninstall catberry       remove the launcher (and ac and mtools, which need it)
```

`catberry TOOL install`, `catberry TOOL uninstall` and `catberry TOOL versions`
work too.

### Build it yourself (Arch-based)

Install what it needs:

```bash
sudo pacman -S --needed base-devel git cmake qt6-base qt6-multimedia qt6-multimedia-ffmpeg layer-shell-qt ffmpeg unzip
```

Then build and install each package (launcher first; skip the tools you don't want):

```bash
git clone -b main https://github.com/CatDesuFR/catberry.git catberry && (cd catberry/packaging/arch && makepkg -si)
git clone -b ac https://github.com/CatDesuFR/catberry.git catberry-ac && (cd catberry-ac/packaging/arch && makepkg -si)
git clone -b mtools https://github.com/CatDesuFR/catberry.git catberry-mtools && (cd catberry-mtools/packaging/arch && makepkg -si)
git clone -b roof-carton-engine https://github.com/CatDesuFR/catberry.git catberry-roofce && (cd catberry-roofce/packaging/arch && makepkg -si)
```

`cmake` and `qt6-base` are for ac, mtools and roofce; the multimedia,
layer-shell, `ffmpeg` and `unzip` packages are for roofce. Other systems: `make install`
on each branch (roofce builds with CMake and needs Qt 6).

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
catberry roofce gui             open the wallpaper manager (Linux, Wayland)
```

`catberry -v` shows which tools are installed:

```
catberry 1.3.3
  ac      2.0.0
  mtools  2.0.0
  roofce  1.2.2
```

`catberry ac ...` runs the `catberry-ac` program, `catberry mtools ...` (and
the `-config` commands) run `catberry-mtools`, and `catberry roofce ...` runs
`catberry-roofce`. The launcher looks for them
next to itself first, then on your PATH. See each branch's README for how the
tools work.

## Build

| Target | Command | Output |
| --- | --- | --- |
| Linux program | `make` | `catberry` |
| Install on Linux | `make install` | `~/.local/bin/catberry` |
| Windows package | `make windows` | `dist/catberry-windows.zip` |
| Arch package | `cd packaging/arch && makepkg` | `catberry-1.3.3-1-x86_64.pkg.tar.zst` |
| Unit tests | `make test` | |
| Every package, tested and published | `ci/release-local.sh catberry` (podman) | a GitHub Release |
| install/uninstall on every distro | `ci/test-install-local.sh` (podman) | |

Ready-built downloads are on the [Releases](https://github.com/CatDesuFR/catberry/releases)
page, each with a `SHA256SUMS` file to check them against. The Windows
package is cross-compiled with MinGW-w64.
