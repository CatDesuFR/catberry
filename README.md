# catberry roofce — Roof Carton Engine

A wallpaper manager for **Linux on Wayland** (KDE Plasma on Fedora, CachyOS,
Garuda; also Hyprland and Sway), version **1.2.2**. Your wallpaper is drawn
by roofce itself, as part of the desktop, not in a window: live videos,
GIFs and pictures, with a Mond-style clock and a music player on top.

This branch holds only roofce. The `catberry` command it runs under lives on
the [`main` branch](https://github.com/CatDesuFR/catberry); install that first.

GNOME is not supported: it does not let apps provide the wallpaper.

## Install

### Fedora

```bash
sudo dnf install qt6-qtbase-devel qt6-qtmultimedia-devel layer-shell-qt-devel cmake gcc-c++ ffmpeg unzip
git clone -b roof-carton-engine https://github.com/CatDesuFR/catberry.git catberry-roofce
cd catberry-roofce && make install
```

`ffmpeg` with full codec support comes from RPM Fusion; it is needed for most
videos (H.264) and for the memory-saving video copies.

### CachyOS, Garuda and other Arch-based distros

One command; pacman also installs the Qt, layer-shell and FFmpeg packages it needs:

```bash
curl -LO https://github.com/CatDesuFR/catberry/releases/download/roofce-v1.2.2/catberry-roofce-1.2.2-1-x86_64.pkg.tar.zst && sudo pacman -U catberry-roofce-1.2.2-1-x86_64.pkg.tar.zst
```

Or build it yourself. First install what it needs:

```bash
sudo pacman -S --needed base-devel git cmake qt6-base qt6-multimedia qt6-multimedia-ffmpeg layer-shell-qt ffmpeg unzip
```

Then build it as a package (recommended):

```bash
git clone -b roof-carton-engine https://github.com/CatDesuFR/catberry.git catberry-roofce
cd catberry-roofce/packaging/arch && makepkg -si
```

Remove it with `sudo pacman -R catberry-roofce`.

Or build and install it just for your user (into `~/.local`), without a package:

```bash
git clone -b roof-carton-engine https://github.com/CatDesuFR/catberry.git catberry-roofce
cd catberry-roofce && make install
```

Remove that one with `make uninstall` in the same folder.

Both put **Roof Carton Engine** in your app menu.

## Use

```
catberry roofce gui     open the wallpaper manager
catberry roofce start   show the saved wallpaper
catberry roofce stop    stop showing it
catberry roofce -v      print the version
```

`gui` returns the terminal straight away. The wallpaper keeps running after
the window is closed, and (if you leave the option on) starts when you log in.

### Wallpaper

The current wallpaper with a live preview, how it fits the screen
(fill, fit, stretch, center), video sound, and start/stop.

### Library

- **My wallpapers**: your uploads and every online wallpaper you set.
- **Wallpaper Engine**: if you own Wallpaper Engine on Steam, the Workshop
  wallpapers you subscribe to appear here, played straight from Steam's
  folder (no copy). Video wallpapers work; scene and web ones do not yet.
- **Online (free)**: MotionBgs (live wallpapers), Wallhaven, Wikimedia
  Commons and NASA, no account needed. Search (it also searches the sites'
  tags, so "eyes" finds wallpapers showing eyes), filter by live or pictures,
  genre, resolution (1080p, 2K, 4K, 8K) and source, and sort by resolution
  or size. Hover a tile to play it; nothing is saved until you set it.
- **Strict filter** and **Hide AI art** are on by default.
- wallpapers.com does not allow apps to use it; **Browse wallpapers.com**
  opens it in your browser, and what you download there can be uploaded.

### Upload wallpaper

Drop images (PNG, JPG, WebP), GIFs or videos (MP4, WebM, MKV, MOV) on the
window, or choose them.

### Widgets

- **Clock** in the style of the Mond Rainmeter skin: position, size, colour,
  font, 12/24-hour, seconds.
- **Music player**: what Spotify is playing (or any other player when
  Spotify is not), with previous / play-pause / next and a progress bar you
  can click. No Spotify login needed.
- Drag both around the desktop (the clock by its text); they stay put.
  **Reset positions** puts them back.

### Font library and Saved fonts

Popular free fonts for the clock, with suggested ones first and **Anurati**
(the Mond font) on top. Click **Download** and a font is installed for you.
Anurati and fonts from other sites: download them in the browser, then
**Saved fonts → Import fonts...** (.ttf, .otf or a .zip).

## Where things are kept

| What | Where |
| --- | --- |
| Settings | `~/.config/catberry/roofce.conf` |
| Uploaded and downloaded wallpapers | `~/.local/share/catberry/roofce/` |
| Fonts | `~/.local/share/fonts/catberry/` |
| Previews and screen-sized video copies (safe to delete) | `~/.cache/catberry/roofce/` |

## Memory

Videos bigger than the screen are copied once at the screen's size in the
background (with ffmpeg) and played from that copy, roughly halving memory
for 4K videos on a 1080p screen.

Wallpapers and fonts belong to their creators; online sources are used for
personal wallpapers only.
