# Quick click Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** mtools 2.1.0 clicks a saved set of screen points, in order, at a 100 µs gap, on its hotkey, from a background process; with a picker, a Quick click tab (Linux), commands on both systems, and launcher 1.3.4 forwarding them.

**Architecture:** Engine in `src/common/quickclick.*` (file format, scaling, the run loop over `Output`); `Output` gains absolute pointer moves (Linux: a third uinput device; Windows: `SendInput` absolute). The background process reuses the hotkey-macro daemon machinery, generalised. Picker: Qt window (Linux), layered Win32 window (Windows). KDE Shake Cursor paused through KWin's D-Bus (Qt DBus) around each run.

**Tech Stack:** C++17, Qt 6 Widgets + DBus (Linux), Win32 (Windows), uinput, podman pipeline.

**Spec:** `docs/superpowers/specs/2026-11-03-quick-click-design.md`

## Global Constraints

- mtools worktree `/home/catdesu/projects/cpp/catberry-mtools`; launcher on `main`.
- File: `quickclicks/<name>.conf` with `desktop=WxH`, `hotkey=`, `gap_us=`, `point=x,y,type` (type `left|right|middle|double`). Names: `validName`.
- Gap: default 100 µs, 100 µs – 1 000 000 µs. UI shows ms with one decimal.
- Pid file name `quick.<name>`.
- uinput pointer device: product `0xcab2`, name "catberry pointer", ABS 0–32767, BTN_LEFT/RIGHT/MIDDLE; `EvdevInput` skips it.
- Shake Cursor: KWin D-Bus `org.kde.KWin` `/Effects` `org.kde.kwin.Effects` `isEffectLoaded`/`unloadEffect`/`loadEffect` with `"shakecursor"`; reload 300 ms after a run; marker `shakecursor-paused` in `runtimeDir()`.
- Commands: `-quickpick NAME HOTKEY [GAP_MS]`, `-quickload NAME`, `-quickunload [NAME]`.
- Versions: mtools 2.1.0, launcher 1.3.4.
- Commits as CatDesuFR, Co-Authored-By line. Publishing needs the user's explicit OK.

## Review Focus

- Our pointer device's events must not come back as global input (evdev skip list). Task 2.
- A run started while one is going: never two runs at once; hotkey during a run stops it. Task 3.
- Shake Cursor left off after a crash: marker file restores it. Task 3.
- Picker cancelled (Esc) must not change a saved set. Task 4.
- Desktop size different from the saved one: points scale. Task 1 test.

---

### Task 1: Quick-click engine data (mtools)

**Files:** Create `src/common/quickclick.h/.cpp`, `tests/test_quickclick.cpp`; register in `tests/main.cpp`/`tests/test.h`; Makefile `test` sources pick it up via wildcard.

**Produces:**
- `enum class ClickType { Left, Right, Middle, Double };`
- `struct QuickPoint { int x, y; ClickType type; };`
- `struct QuickSet { std::vector<QuickPoint> points; int desktopW = 0, desktopH = 0; int hotkey = -1; int gapUs = 100; };`
- `const int kMinGapUs = 100, kMaxGapUs = 1000000;`
- `bool writeQuickSet(const std::string &name, const QuickSet&)`, `bool readQuickSet(const std::string &name, QuickSet&, std::string &error)`, `std::vector<std::string> quickSetNames()`, `bool deleteQuickSet(const std::string&)`, `std::string quickPidName(const std::string &name)` (= `"quick." + name`).
- `QuickPoint scaledPoint(const QuickPoint&, int fromW, int fromH, int toW, int toH)` (rounded; unchanged when sizes equal or from is 0).
- `const char *clickTypeName(ClickType)` / `bool parseClickType(const std::string&, ClickType&)`.

- [ ] Tests: round trip of a set with 3 points (all types), gap and hotkey; gap clamped into range on read; bad point line → `readQuickSet` false with an error naming the line; `scaledPoint({960,540},1920,1080,3840,2160) == {1920,1080}`; names sorted; delete; `quickPidName("x") == "quick.x"`.
- [ ] `make test` fails → implement → passes → commit `mtools: quick-click sets (file format, scaling)`.

### Task 2: Pointer output

**Files:** `src/common/output.h`, `src/linux/output.cpp`, `src/linux/evdev.cpp`, `src/linux/linux.h`, `src/windows/input.cpp` (or wherever Windows `createOutput` lives).

**Produces:** in `Output`: `virtual bool canPoint() const { return false; }`, `virtual void moveTo(int x, int y, int desktopW, int desktopH) {}`, `virtual void pointerButton(int code, bool down) {}` (mouse button through the pointer device, so it is ordered with the moves). Linux `UinputOutput` creates the pointer device when asked (`createOutput(backend, withKeyboard, error, withPointer=false)` — new defaulted parameter) and implements them; `kPointerProduct = 0xcab2` skipped in evdev. Windows: `SendInput` absolute over the virtual screen.

- [ ] Check: `make` and `make windows` build; `make test` passes; a tiny manual check runs in Task 3's container smoke (pointer device created when uinput exists).
- [ ] Commit `mtools: absolute pointer output`.

### Task 3: Run loop, background process, commands, Shake Cursor

**Files:** `src/common/quickclick.cpp` (run loop), `src/common/daemon.h` + `src/linux/daemon.cpp` + `src/windows/daemon.cpp` (generalise launch to a "kind": config or quick), `src/common/quickcmd.cpp` (commands), `src/linux/shakecursor.cpp` + header (Qt DBus; no-op off KDE), `src/main.cpp` (dispatch; Windows `--serve-quick NAME`), `CMakeLists.txt` (Qt6::DBus).

**Produces:**
- `int serveQuickSet(const std::string &name, const QuickSet&, const std::function<void(const std::string&)> &ready)`: KeyWatcher on the hotkey (mouse if side button); on press: pause Shake Cursor, for each point `scaledPoint` → `moveTo` → click(s) via `pointerButton` → wait `gapUs` (`sleepUntil`), stop early if the hotkey is pressed again; schedule the Shake Cursor reload 300 ms later.
- `bool launchQuickDaemon(const std::string &name, const QuickSet&, std::string &error)` (pid name `quick.<name>`).
- `int quickLoad(int, char**)`, `int quickUnload(int, char**)`, `int quickPick(int, char**)` (Linux: Qt picker from Task 4; Windows: Task 6).
- `class ShakeCursorPause { void pause(); void resumeLater(); static void restoreIfMarked(); }`.

- [ ] `make test`; then container check via `ci/release-local.sh mtools --no-publish` later (Task 7). Locally: `-quickload` of a set with no input access fails with the input-group message; `-quickunload` with nothing loaded prints "No quick clicks are loaded."
- [ ] Commit `mtools: quick-click background process and commands`.

### Task 4: Point picker (Linux Qt)

**Files:** `src/qt/pickerwindow.h/.cpp`.

**Produces:** `bool pickPoints(QuickSet &set)` — modal; shows existing points; returns true on Enter with ≥1 point, false on Esc (set untouched); fills `desktopW/H` from the union of `QScreen` geometries.

- [ ] Offscreen check: `CATBERRY_SCREENSHOT` picture of the picker with 3 points (hook in the picker: when `CATBERRY_PICK_QUIT_MS` is set it saves a picture and returns false after that long) → looks right.
- [ ] Commit `mtools: point picker`.

### Task 5: Quick click tab (Linux)

**Files:** `src/qt/quicktab.h/.cpp`, `src/qt/quickdialog.h/.cpp`, `src/qt/mtoolswindow.cpp` (third tab, global input to the dialog).

As the spec's tab section; Load/Unload like `HotkeysTab` (fresh `-quickload` process, `stopProcess`, `forgetLoaded` with the pid name).

- [ ] `tests/smoke.sh` adds a saved quick set; screenshot of tab 3 checked.
- [ ] Commit `mtools: Quick click tab`.

### Task 6: Windows picker

**Files:** `src/windows/picker.cpp` (layered top-most window over the virtual screen, same keys and drawing as the spec).

- [ ] `make windows` (container) compiles without warnings. Ruling: not run.
- [ ] Commit `mtools: Windows point picker`.

### Task 7: Versions, docs, launcher, release

- mtools 2.1.0 (`kVersion`, PKGBUILD), README Use section; packaging unchanged (Qt DBus is in `qt6-base`; Debian/Ubuntu deps come from dpkg-shlibdeps).
- Launcher 1.3.4: forward `-quickpick`, `-quickload`, `-quickunload` to mtools (`args.cpp`, test), help text, README.
- [ ] `make test` (both), `ci/release-local.sh mtools --no-publish` all OK, `ci/test-install-local.sh` OK.
- [ ] Commit, push; final review (fresh reviewer); fix; ask the user to publish; publish; user's desktop check.
