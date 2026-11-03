# ac and mtools in Qt 6 (Wayland) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On Linux, `catberry-mtools gui` and `catberry-ac gui` open native Qt 6 Wayland windows with everything the old windows did, plus saved-macro and hotkey-macro management; released as 2.0.0.

**Architecture:** Per branch (`mtools` worktree `/home/catdesu/projects/cpp/catberry-mtools`, `ac` worktree `/home/catdesu/projects/cpp/catberry-ac`): Linux builds with CMake + Qt 6 Widgets from the unchanged engines (`src/common` minus the drawn-window files, `src/linux` minus `host.cpp`) plus new `src/qt/`. `make windows` is untouched. mtools first (it has more), then ac reuses the `src/qt` shared pieces by copy.

**Tech Stack:** C++17, Qt 6 Widgets, CMake, evdev/uinput (existing), podman release pipeline on `main`.

**Spec:** `docs/superpowers/specs/2026-11-03-ac-mtools-wayland-qt-design.md` (on `main`)

## Global Constraints

- Linux build excludes `src/common/gui.cpp`, `src/common/ac.cpp` (ac) / `src/common/mtools.cpp` (mtools) and `src/linux/host.cpp`; those stay for Windows. `src/qt/` provides `int acGui()` / `int mtoolsGui()` (the symbols `main.cpp` calls).
- No X11: `linux/output.cpp` keeps only uinput; libX11/libXtst gone from builds and packages.
- Theme colours exactly: bg `#1e1e2e`, text `#cdd6f4`, button `#313244`, border `#45475a`, accent `#89b4fa`, go `#a6e3a1`, stop/warn `#f38ba8`, panel `#181825`.
- Window titles: "Catberry Autoclicker", "Catberry Macro Recorder". Desktop entries: `catberry-ac.desktop` (Icon `input-mouse`), `catberry-mtools.desktop` (Icon `media-record`), Categories `Utility;`.
- Fix commands shown verbatim: `sudo usermod -aG input $USER`, `catberry install catberry`.
- Settings files unchanged in name and format.
- Versions 2.0.0 (`kVersion` in `src/main.cpp`, `pkgver` in PKGBUILD).
- Hidden option `gui --quit-after MS`.
- Commits authored `CatDesuFR <stefan.zara.tudor@gmail.com>`, ending with the Co-Authored-By line, on the `mtools` / `ac` / `main` branches.

## Review Focus

- Our own uinput clicks arriving back as global input must not toggle anything (evdev already skips our device IDs; the Qt layer must not re-add Qt mouse events as "global"). Checked by reading + desktop test.
- A capture button waiting for a key must not also fire the hotkey (300 ms ignore window as today). Task 3 / Task 5 code paths.
- Closing the window while playing/clicking stops output threads before the Output is destroyed (Player/Clicker destructors run first). Task 3 / Task 5: member order.
- Hotkey macros keep running after the window closes (daemon is separate). Task 4 offscreen smoke + desktop test.
- Missing `/dev/input` or `/dev/uinput`: window still opens and shows the banner instead of exiting. Task 2 offscreen run in a container (no input devices there).

---

### Task 1: mtools engine additions + `make test`

**Files (mtools):** Modify `src/common/macro.h/.cpp`; create `tests/test.h`, `tests/main.cpp`, `tests/test_macro.cpp`; Makefile `test` target (sources: `src/common/{keys,macro}.cpp src/linux/system.cpp` + tests; whatever else `macro.cpp` needs to link, e.g. `src/linux/daemon.cpp` → if it pulls output/evdev, include `src/linux/*.cpp` except `host.cpp`, with X11 still linked until Task 2).

**Produces:** `std::vector<std::string> macroNames();`, `std::vector<std::string> configNames();` (sorted, from `configDir()/macros/*.macro` and `configs/*.conf`), `bool deleteMacro(const std::string&)`, `bool renameMacro(const std::string &from, const std::string &to)` (false if `to` exists or `from` missing or `to` invalid), `bool deleteConfig(const std::string&)`.

- [ ] Test (temp `XDG_CONFIG_HOME` via `setenv` before first `configDir()`): save macros `b`, `a` → `macroNames() == {"a","b"}`; rename `a`→`c` true, `c`→`b` false; delete `b` → `{"c"}`; `writeConfig("x", cfg)` then `readConfig` gives same keys/hotkey/count/delay; `configNames() == {"x"}`; `deleteConfig("x")` → empty.
- [ ] `make test` fails → implement → passes → commit `mtools: list, rename and delete saved macros and configs; make test`.

### Task 2: mtools Qt build and shared window pieces

**Files (mtools):** Create `CMakeLists.txt`, `src/qt/theme.h/.cpp`, `src/qt/globalinput.h/.cpp`, `src/qt/keybutton.h/.cpp`, `src/qt/problems.h/.cpp`, `src/qt/mtoolswindow.h/.cpp` (empty tabs for now), `data/catberry-mtools.desktop`; modify `Makefile` (Linux targets wrap CMake like roofce; `test` keeps building with g++), `src/linux/output.cpp` (uinput only), `src/main.cpp` (`gui [--quit-after MS]`), `.gitignore` (`/build/`); delete `src/linux/host.cpp`.

**Produces:**
- `const char *catberryStyleSheet();`
- `class GlobalInput : public QObject { bool available() const; signals: void input(InputEvent); }` (one `QSocketNotifier` per evdev fd; rescans every 2 s for new devices)
- `class KeyButton : public QPushButton { int code() const; void setCode(int); void setForbidden(int code); bool capturing() const; signals: void changed(int); }` — click → "Press a key..."; next key (`nativeScanCode()-8`, modifiers ignored, Esc cancels, forbidden code ignored); optional `acceptGlobal(InputEvent)` for mouse buttons; after a capture, `recentlyCaptured()` true for 300 ms.
- `class ProblemsBanner : public QFrame { void setInputMissing(bool, const QString &whatStops); void setOutputMissing(bool); }` — red text + read-only command + Copy button per problem; hidden when none.
- `int mtoolsGui(int quitAfterMs);` (main passes 0 normally)

- [ ] Run `cmake -S . -B build && cmake --build build` → fails (no CMakeLists) — then write it and the pieces.
- [ ] `build/catberry-mtools -v` prints 1.1.0; `QT_QPA_PLATFORM=offscreen build/catberry-mtools gui --quit-after 500` exits 0; `make test` passes; `make windows` untouched (check `git diff Makefile` keeps the windows rules).
- [ ] Commit `mtools: Qt 6 build, drop X11; shared window pieces`.

### Task 3: mtools Recorder tab

**Files:** `src/qt/mtoolswindow.cpp` (+ `.h`).

Behaviour exactly as the spec's Recorder section (record/play with global hotkeys and Qt-focus fallback, 3 s countdown on the Play button, Repeat spin box, two KeyButtons that must differ, saved-macros list with load on click, Save as…/Rename…/Delete with `QInputDialog`/`QMessageBox`, status line, settings read/written to `mtools.conf` in the same keys as `common/mtools.cpp` (`record_key`, `play_key`, `repeat`, ... — copy its load/save code), banner). Recording logic (pressed-key bookkeeping, dropping unreleased downs) copied from `common/mtools.cpp`.

- [ ] Offscreen run with a pre-made `macros/test.macro` in a temp `XDG_CONFIG_HOME`: `gui --quit-after 500` exits 0 (no crash with a list entry). Add `tests/smoke.sh` doing this.
- [ ] Commit `mtools: Recorder tab`.

### Task 4: mtools Hotkey macros tab

**Files:** `src/qt/configdialog.h/.cpp`, `src/qt/mtoolswindow.cpp`.

Behaviour exactly as the spec's Hotkey macros section (table, 1 s refresh via `loadedPid`, New…/Edit… dialog with live `parseKeyList` check, Delete unloads first, Load via `launchDaemon`, Unload / Unload all via `stopProcess` + `forgetLoaded` as `configcmd.cpp` does, reload offer after editing a running one).

- [ ] `tests/smoke.sh` also creates `configs/x.conf` (via `catberry-mtools -config x E,LMB F6 1`) and opens the window offscreen → exit 0.
- [ ] Commit `mtools: Hotkey macros tab`.

### Task 5: ac in Qt

**Files (ac):** Copy `src/qt/{theme,globalinput,keybutton,problems}.*` from mtools; create `src/qt/acwindow.h/.cpp`, `CMakeLists.txt`, `data/catberry-ac.desktop`, `tests/smoke.sh`; modify `Makefile`, `src/linux/output.cpp` (uinput only), `src/main.cpp` (`--quit-after`), `.gitignore`; delete `src/linux/host.cpp`.

Behaviour exactly as the spec's Autoclicker section; settings load/save copied from `common/ac.cpp` (incl. the legacy keys).

- [ ] Build; `-v`; offscreen `gui --quit-after 500` exit 0 (with and without an `ac.conf` using `target=key`).
- [ ] Commit `ac: Qt 6 window, drop X11`.

### Task 6: Packaging, 2.0.0, docs, pipeline

**Files:** both branches: `packaging/arch/PKGBUILD` (2.0.0, deps), `packaging/stage.sh` (CMake; installs program, desktop file, README), `packaging/fedora/*.spec`, `packaging/debian/control` (`qt6-wayland` + comment), `src/main.cpp` `kVersion`, README (Use + build). `main`: `ci/build.sh` deps for ac/mtools, `ci/smoke.sh` (ac/mtools: offscreen `gui --quit-after 1000`), launcher `src/manage.cpp` `looseCopies` adds `dataHome()/applications/catberry-<tool>.desktop` for ac/mtools (+ test line in `ci/test-install.sh`).

- [ ] `ci/release-local.sh mtools --no-publish` and `ac --no-publish` → all builds and smoke tests OK (needs the `main` changes pushed first, since sources come from `origin`).
- [ ] Commit and push the three branches.

### Task 7: Release and desktop check

- [ ] `ci/release-local.sh mtools`, then `ac` → `mtools-v2.0.0`, `ac-v2.0.0`.
- [ ] Ask the user to install them (`sudo pacman -U` from the releases) and check on their Wayland desktop: clicks land in a native Wayland window, F6 works while another window is focused, record/play, Load/Unload a hotkey macro.
