# Catberry's Mod Menu

A Geode mod menu for Geometry Dash **2.2091** (Geode **v6.0.0-alpha.1**), Windows build (works on Linux through Proton).

Press **Tab** in game to open the menu.

## Features

| Area | What's in it |
|---|---|
| Click Before Frames | Early clicks (within N ticks) are kept and applied on the first tick they can jump or hit an orb. Each saved click is appended to `CBF-LOGS.txt`. |
| Cheat indicator | Follows Pointercrate's official "Allowed Mods/Programs" sheet. Green / orange / red, and stays red for the rest of an attempt once a disallowed hack was on. |
| Console | Live window with everything this mod logs. Filter by level, copy, clear. |
| Global | Speedhack (+ audio sync), Audio Speed, FPS Bypass, Safe Mode, Auto Safe Mode, Transition Speed |
| Player | Noclip (P1/P2), No Death Effect, No Respawn Flash, Mute Level Audio on Death, No Trail, Always Show Trail, Solid Wave Trail, No Robot Fire, No Vehicle Particles, Hide Player, Jump Hack, AutoClicker |
| Level | StartPos Switcher (Q/E), Accurate Percentage, Show Hitboxes, Hitboxes On Death, Auto Practice Mode, No Particles, No Object Glow, No End Shake, Hide Pause Menu |
| Bypass | Unlock Icons, Practice Music Sync, Level Copy Bypass, Character Limit Bypass, Character Filter Bypass |
| Creator | Free Scroll |
| Labels | Cheat Indicator, FPS, CPS + clicks, Attempt, Percentage, Run From, Best Run (startpos aware), Level Time, Session Time, Clock, Testmode, StartPos, Noclip Accuracy, Noclip Deaths, CBF |

## Where files go

Both are in the mod's save folder (`<GD folder>/geode/mods/catdesu.cats-mod-menu/`):

- `CBF-LOGS.txt`: one line per click saved by Click Before Frames, e.g.
  `[2026-11-07 21:03:11] Bloodbath (10565740) att 42 @ 37.21% - 2 ticks early, P1 jump`
- `imgui.ini`: menu window positions.

## Pointercrate statuses

Statuses are taken from the Eclipse (v1.9.4) tab of Pointercrate's
[Allowed Mods/Programs sheet](https://docs.google.com/spreadsheets/d/1evE4nXATxRAQWu2Ajs54E6cVUqHBoSid8I7JauJnOzg/edit)
(last updated 10/02/26). Hacks that aren't on that sheet, like Click Before Frames, count as not allowed, as the sheet says.
They live in `src/core/HackList.cpp` if the sheet changes.

## Building

The GitHub Actions workflow builds the Windows `.geode` on every push. Download it from the run's artifacts.

Locally you need the Geode CLI 4.0+ and the v6.0.0-alpha.1 SDK:

```bash
geode build
```

## Credits

- Menu UI: [Dear ImGui](https://github.com/ocornut/imgui) through [gd-imgui-cocos](https://github.com/matcool/gd-imgui-cocos)
- Font: Roboto (Apache License 2.0)
- Hack list inspired by Eclipse Menu's feature list (no code copied)
