# Quick click (mtools 2.1.0)

A new mtools tool: pick points on the screen, give the set a hotkey; every
press of the hotkey clicks all the points in order, as fast as the desktop
reliably takes it. Like the hotkey macros, sets are saved by name, loaded
into a background process and keep working after the window is closed.

## What the probes showed (developer's PC, KDE Plasma 6.8.2 Wayland, one 1920x1080 screen, 100 %)

- An absolute uinput pointer (`ABS_X`/`ABS_Y`, range 0–32767, plus
  `BTN_LEFT`) lands within 0.1 px of the target: the compositor maps the
  range onto the whole desktop. No calibration needed.
- Jump + click with **no gap**: about half the clicks are dropped or land
  at the previous point. With a gap of **100 µs** or more between points:
  50 of 50 every time (3 runs each at 100, 250, 500 and 1000 µs).
  50 points at 100 µs take about 8 ms.
- KDE's **Shake Cursor** effect (`shakecursor`, on by default in Plasma 6)
  enlarges the pointer during fast jumps.
- Not measured: other scaling, several screens, other desktops.

## Behaviour

- A **quick-click set**: points in order, each with a click type — Left,
  Right, Middle or Double (two left clicks) — a hotkey (a key or a side
  mouse button, like hotkey macros), and the **gap** between points.
- Gap: default **100 µs**, range 100 µs – 1 s (shown in the UI as
  "0.1 ms" … "1000 ms"; 0.1 ms steps). Below 100 µs is not offered (clicks
  get dropped).
- Pressing the hotkey: for each point, jump there, click (press, release;
  Double = press, release, press, release), then wait the gap. The pointer
  stays at the last point. Pressing the hotkey again while a run is going
  stops it after the current point. Runs never overlap.
- KDE: if the `shakecursor` effect is loaded, it is unloaded just before a
  run and loaded again 300 ms after the run ends (once per run, not per
  point; a new run within those 300 ms keeps it off). Only when it was
  loaded; elsewhere nothing happens. If mtools stops unexpectedly while it
  is off, the next run or the next start of the background process loads
  it again (a marker file in the runtime folder says it was paused).
- Positions are stored in desktop pixels together with the desktop size
  when they were picked. If the desktop size differs at run time, points
  are scaled proportionally.
- The desktop is the bounding box of all screens (Qt: `QScreen` geometry
  union; Windows: the virtual screen).

## Files

`~/.config/catberry/quickclicks/<name>.conf` (Windows:
`%APPDATA%\catberry\quickclicks\`), names as for configs (letters, digits,
`_`, `-`):

```
desktop=1920x1080
hotkey=F7
gap_us=100
point=192,108,left
point=960,540,double
```

Loaded sets have a pid file in the runtime folder named
`quick.<name>.pid` (a `.` cannot appear in a config name, so it never
clashes with hotkey macros).

## Output

- `Output` gains `virtual bool canPoint() const` and
  `virtual void moveTo(int x, int y, int desktopW, int desktopH)`
  (default: not supported).
- Linux: a third uinput device "catberry pointer" (same vendor id, new
  product id `0xcab2`, skipped by `EvdevInput` like the others):
  `EV_ABS` `ABS_X`/`ABS_Y` 0–32767, `EV_KEY` `BTN_LEFT`/`BTN_RIGHT`/`BTN_MIDDLE`.
  Clicks of a quick click go through this device (so press and jump are
  one device, in order).
- Windows: `SendInput` with `MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE |
  MOUSEEVENTF_VIRTUALDESK` (0–65535 over the virtual screen) and the
  button flags.

## Commands (mtools; the launcher forwards them like `-config`)

```
catberry -quickpick NAME HOTKEY [GAP_MS]   pick the points of a new or existing set (opens the picker)
catberry -quickload NAME                   start it in the background
catberry -quickunload [NAME]               stop one, or all
```

`GAP_MS` accepts decimals (`0.1`). `-quickpick` on an existing set keeps
its points as the starting picture (they can be removed and re-added).
Messages follow `-config`'s ("Saved quick click 'x': F7 clicks 5 points.",
"Load it with: catberry -quickload x").

## The point picker

A see-through full-screen layer over the whole desktop (Linux: a Qt
window, full screen on each screen, on top; Windows: a layered top-most
window over the virtual screen), showing the points as numbered dots
joined by lines, and a bar at the top:
"Click to add a point (Left) · L/R/M/D: click type · Backspace: remove
last · Enter: save · Esc: cancel".

- Click: add a point with the current type at that spot.
- L, R, M, D: the type for the next points (shown in the bar).
- Backspace: remove the last point. Enter: save (at least one point).
  Esc: cancel, nothing changes.

## Quick click tab (Linux Qt, Macro recorder window)

- Table: Name, Points, Hotkey, Gap, Status (Running / Stopped), refreshed
  every second, like Hotkey macros.
- **New…**: a dialog for name, hotkey (key button, side mouse buttons
  too), gap; Next opens the picker; saving writes the set.
- **Edit…**: the same dialog with the points table (#, x, y, click type as
  a dropdown, ↑ / ↓ / Remove) and **Pick again…** (opens the picker with
  the current points). Editing a running set offers to reload it.
- **Delete** (unloads first), **Load** (runs `catberry-mtools -quickload
  NAME`, as the Hotkey macros tab does), **Unload**, **Unload all**.
- The tab says "Quick click moves the pointer: it needs /dev/uinput" when
  output is missing (the banner already covers it) and nothing else
  differs without global input except that hotkeys cannot be heard: Load
  then fails with the input-group message.

## Versions

- mtools **2.1.0**, launcher **1.3.4** (forwards `-quickpick`,
  `-quickload`, `-quickunload`; help text).
- READMEs: mtools Use section; launcher help and README command list.

## Testing

- `make test` (mtools): file round trip, scaling to another desktop size,
  name rules, pid naming.
- Offscreen: the window with a saved set (all three tabs) opens and closes
  (`tests/smoke.sh`, CATBERRY_SCREENSHOT for checking the tab).
- Release pipeline: build and test-install on all four distros + Windows
  compile.
- On the developer's desktop (by the user): picker over the screen, a set
  of points clicking the right places, the pointer not growing, Load
  keeping it working after closing the window, Unload.

## Out of scope

- Recording a quick-click set from real clicks.
- Per-point delays.
- Moving the pointer back after the run.
- A Quick click screen in the old Windows window (Windows has the
  commands and the picker).
