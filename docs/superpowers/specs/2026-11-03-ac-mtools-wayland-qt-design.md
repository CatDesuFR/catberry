# ac and mtools: native Wayland apps in Qt 6

Project 3 of 3 (after the release pipeline and `catberry install`). The
Linux builds of the autoclicker (`ac` branch) and the macro recorder
(`mtools` branch) get new Qt 6 windows; they become their own apps in the
app menu. Windows builds stay as they are.

## Goal

On a Wayland desktop, `catberry ac gui` and `catberry mtools gui` (or the
app-menu entries) open native Qt 6 windows, no X11/XWayland. Everything the
current windows do still works, the macro recorder also manages hotkey
macros (`-config`) and saved macros from its window, and settings files stay
compatible.

## What stays, what goes

Stays (reused as is):

- The engines: `common/clicker`, `common/keys`, `common/macro`,
  `common/player`, `common/configcmd` (the `-config*` commands),
  `common/daemon.h` + `linux/daemon.cpp` (background loaded configs),
  `linux/evdev.cpp` (global keyboard/mouse reading, needs the `input`
  group), `linux/system.cpp` (config folder, runtime folder).
- Settings and file formats: `ac.conf`, `mtools.conf`, `macros/*.macro`,
  `configs/*.conf`, all under `~/.config/catberry/`.
- The command lines (`gui`, `-v`, `-help`, `-config*`, `--serve`), and the
  launcher integration.
- The Windows builds: `src/windows/*`, `common/gui.*`, `common/ac.cpp` /
  `common/mtools.cpp` (the drawn window logic) and `make windows`.

Goes (Linux only):

- `linux/host.cpp` (the X11 window and key grabs).
- XTest output in `linux/output.cpp`; output is uinput only. Without
  access to `/dev/uinput` (the udev rule from the launcher package), the
  window says so, with the fix.
- libX11 / libXtst build and package dependencies.

## Build (Linux)

- CMake + Qt 6 Widgets, like roofce: `CMakeLists.txt` builds
  `catberry-<tool>` from `src/common/*` (minus the Windows-only window
  files), `src/linux/*` (minus `host.cpp`) and the new `src/qt/*`.
- `Makefile`: `make` / `make install` / `make uninstall` wrap CMake (as on
  roofce); `make windows` unchanged; new `make test` (non-window logic).
- `make install` also installs the `.desktop` file to
  `~/.local/share/applications/`.

## Qt windows

Shared look: the roofce theme (Catppuccin Mocha: background `#1e1e2e`, text
`#cdd6f4`, buttons `#313244` / border `#45475a`, accent `#89b4fa`, green
`#a6e3a1` for Start/Play, red `#f38ba8` for Stop/warnings). Sizes in layout
units, so it scales with the display. Global input arrives from
`EvdevInput` through a `QSocketNotifier` per device fd, on the window's
thread; engine callbacks from other threads (Player done) are queued to it.

### Autoclicker (`catberry ac gui`), title "Catberry Autoclicker"

- **Clicks per second**: `QDoubleSpinBox`, 0.1–1000, step 1, typed values
  kept (`%g` formatting as today).
- **Click with**: exclusive toggle buttons Left, Middle, Right, Scroll up,
  Scroll down, **Key…** (click, then press a key; label becomes
  `Key: E`; Esc cancels; cannot be the hotkey).
- **Start/stop hotkey**: a button with the key name (default F6); click,
  press a key (modifiers ignored, Esc cancels, cannot be the pressed key).
- **Start**: big green button; 3-second countdown ("Starting in 3...
  (click to cancel)"), clicking cancels; red **Stop** while running. The
  hotkey starts/stops at once. While running, any click on the window
  stops (as today: our own clicks may land on it).
- **Click test**: a box; while running and hovered, counts the clicks that
  arrive and shows "Click test: 12.3 CPS received (45)"; with a key target:
  "Click test: counts mouse clicks only".
- **Status**: "Stopped" / "Running - F6 or click this window to stop" /
  "Move the pointer to where it should click".
- **Problems banner** (red, only when needed), each with a copy button:
  - no readable `/dev/input`: "The hotkey only works while this window is
    focused. To make it global, run this and log out and back in:"
    `sudo usermod -aG input $USER`
  - no `/dev/uinput`: "Clicking is not possible: this user cannot use
    /dev/uinput. Install the catberry launcher package (it adds the rule):"
    `catberry install catberry`
- Without global input, the hotkey still works while the window has focus
  (Qt key events).

### Macro recorder (`catberry mtools gui`), title "Catberry Macro Recorder"

Two tabs.

**Recorder**

- **Record** (F8) / **Stop recording**; **Play** (F9) / **Stop**; Play
  button waits 3 seconds (countdown), the hotkey plays at once; clicking
  the window during playback stops it.
- **Repeat**: `QSpinBox` 0–100000, 0 = until stopped (default 1).
- **Record hotkey**, **Play hotkey**: key buttons as in ac (must differ).
- **Saved macros**: a list, one row per `macros/*.macro`: name, events,
  length ("E-taps — 42 events, 3.1 s"), sorted by name. Clicking a row
  loads it. Buttons **Save as…** (asks a name; names: letters, digits,
  `_`, `-`; asks before overwriting), **Rename…**, **Delete** (asks).
- **Status**: as today ("Recording... 12 events (F8 to stop)", "Ready: 42
  events, 3.1 s", "Nothing recorded yet", messages after save/load).
- Recording needs global input; without it the banner (as ac's first
  item, saying "Recording and hotkeys are off") and Record is disabled.

**Hotkey macros**

- Table of `configs/*.conf`: Name, Keys (`E,LMB`), Hotkey, Count, Delay
  (ms), Status (**Running** / Stopped, from `loadedPid`), refreshed every
  second.
- **New…** / **Edit…**: a dialog with Name (validated as above; fixed when
  editing), Keys (text, checked with `parseKeyList` as you type, the bad
  entry named), Hotkey (key button, also accepts mouse buttons MB4/MB5 via
  global input), Count (1–1000), Delay (1–1000 ms, default 15). Saved with
  `writeConfig`. Editing a running config: saved, and it asks to reload it.
- **Delete** (asks; unloads it first if running), **Load** (as
  `-configload`: `launchDaemon`; keeps running after the window closes),
  **Unload**, **Unload all**. Errors shown in a message box.

### Shared pieces (`src/qt/`)

- `theme.*`: the stylesheet.
- `keybutton.*`: a button that captures the next key (Qt key event,
  `nativeScanCode() - 8` = evdev code; falls back to `Qt::Key` mapping when
  the scan code is 0) or, from global input, a mouse button when allowed.
- `globalinput.*`: wraps `EvdevInput` with socket notifiers; signal
  `input(InputEvent)`; `available()`.
- `problems.*`: the banner.
- `acwindow.*`, `mtoolswindow.*`, `configdialog.*`.

## Engine additions (`common/macro`)

- `std::vector<std::string> macroNames();` / `configNames();` — sorted,
  from the folders.
- `bool deleteMacro(name)`, `bool renameMacro(from, to)` (false if `to`
  exists), `bool deleteConfig(name)`.

## App menu

- `data/catberry-ac.desktop`: Name "Catberry Autoclicker", Exec
  `catberry-ac gui`, Icon `input-mouse`, Categories `Utility;`.
- `data/catberry-mtools.desktop`: Name "Catberry Macro Recorder", Exec
  `catberry-mtools gui`, Icon `media-record`, Categories `Utility;`.
- Installed by every package to `/usr/share/applications/` and by
  `make install` to `~/.local/share/applications/`.
- `catberry uninstall ac|mtools` removes the `~/.local` copy of the entry
  (launcher change on `main`, like roofce's).

## Packaging and release

- Versions **2.0.0** (ac and mtools), `-v` and PKGBUILD.
- Arch: `depends=('catberry' 'qt6-base')`, `makedepends=('cmake' 'gcc')`.
- Fedora spec: `BuildRequires: cmake gcc-c++ qt6-qtbase-devel`.
- Debian/Ubuntu: build deps `cmake qt6-base-dev`; runtime from
  `dpkg-shlibdeps` plus `qt6-wayland` (the Wayland platform plugin, loaded
  at runtime).
- `packaging/stage.sh` builds with CMake and installs the program, the
  `.desktop` file and the README.
- `ci/build.sh` on `main`: ac/mtools extra build deps become
  `cmake qt6-base` (Arch), `cmake qt6-base-dev` (Debian/Ubuntu); Fedora via
  `dnf builddep`.
- `ci/smoke.sh`: for ac and mtools also start `gui` with
  `QT_QPA_PLATFORM=offscreen` and quit it after a moment (see Testing).
- READMEs: Use sections describe the new windows; build sections the new
  dependencies.

## Testing

- `make test` on each branch: macro/config listing, rename, delete, and
  config read/write round trip, in a temporary `XDG_CONFIG_HOME`.
- A hidden `--quit-after MS` option on `gui` (closes the window after MS
  milliseconds) so the smoke tests can open and close the real window
  under `QT_QPA_PLATFORM=offscreen` in every distro container; a crash or
  missing library fails the release.
- The release pipeline's container installs (all four distros).
- On the developer's Wayland desktop (by the user): clicks land in native
  Wayland windows, the hotkeys work while other windows are focused,
  recording and playback, Load/Unload of a hotkey macro.

## Out of scope

- Windows UI changes.
- Recording mouse movement.
- The Global Shortcuts portal.
