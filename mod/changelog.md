# v0.13.0

- Renamed to Catberry's Mod Menu (same mod ID, so settings, macros and keybinds stay).
- New: Frame Window Analyser + Frame Window Counter (CBF window), in the style of NaN GD's frame window counter videos. For each click of the replay CBF follows, the hidden player is run from your real state with that click moved earlier and later, tick by tick; its window is how many ticks in a row it survives until just before the next click (a press moves together with its release). The counter draws a ring and the number at every click in NaN GD's colours (1 red, 2 orange, 3 yellow, 4 white, 5-6 green, 7-8 light blue, 9-10 blue) with a tally per colour. Windows over 10 are hidden like NaN GD's (option). This replaces the old Prediction.
- Click Before Frames uses the analyser: it now aims for the earliest tick of a click's window instead of the replay's exact tick, and clicks inside the window go through on exactly the tested tick. Tested in game: presses up to 52 units too early survived (before: 49), presses inside the window survived, 60 units early (out of reach) died.
- Fix: All Passable also turned off spikes and other hazards (it worked like noclip). Now only solid blocks and slopes are passed.
- Labels: Best %, Total Attempts, Objects, Session Deaths, Speed.

# v0.12.0

Fifth batch of QOLMod / Eclipse modules, written from scratch:
- Hitbox Multiplier (cheat): bigger or smaller hitboxes for the player, hazards and solids separately. Show Trajectory and the hitbox view follow it. Tested in game.
- Unlock All Items: every shop / reward item counts as unlocked (music customiser, practice music sync...).
- Hide Pause Button.
- Safe Mode on End Screen: writes "Safe Mode" on the level complete screen when the run couldn't save.
- Show Trajectory also draws player 2's paths in dual.
- Menu: Favourites. Shift + right-click a hack to star it; starred hacks get their own window.

# v0.11.0

Fourth batch of QOLMod / Eclipse modules, written from scratch:
- Chest Bypass: secret and special chests count as unlocked.
- Instant Restart: respawn right away after dying.
- End Screen Stats: jumps, attempt time and (with noclip) deaths and accuracy on the level complete screen.
- No Object Fade (cheat): objects that fade out or are hidden stay fully visible.
- Vertical Sync toggle.
- Keys label: the keys you're holding.
- Show Hitboxes: "Hide player icon" option.

# v0.10.0

Third batch of QOLMod / Eclipse modules, written from scratch:
- All Passable (cheat): you pass through solid blocks. Tested in game: a wall hit the player with it off, and was passed with it on.
- No Circles: hides the circle effects from orbs, pads and portals.
- Player Particles: hide each kind of player particle separately (ground, trailing, ship click, vehicle ground, UFO click, robot burst).
- No Particle Triggers, Hide Attempt Label, No Music Fade Out.
- Auto Song Download: downloads a level's song when you open its page.
- Hide Editor UI (bind it to a key to toggle).

# v0.9.0

Second batch of QOLMod / Eclipse modules, written from scratch:
- Show Trajectory (cheat): where you'd go if you held (green) or let go (red) from now on, with a dot where that path dies. Uses the same hidden-player simulation that matched the real player exactly in testing. Length option.
- Layout Mode (cheat): hides decoration.
- Pitch Shift: changes pitch without changing speed (music only or all audio).
- Hide % on Complete, Menu Snow.
- No Dash Fire, No Swing Fire, No Wave Pulse, Wave Trail Size.
- Labels: Practice / Testmode, Jumps, Position, Gamemode (mode, speed, mini).
- StartPos Switcher: on-screen arrow buttons (option).
- Noclip: flash colour (hex). Auto Practice Mode: "demons only" option.

# v0.8.0

- Click Before Frames: new stages 32, 64, 96 and 128 ticks (same for every gamemode).
- Cheat indicator: no more orange. "Sometimes allowed" hacks count as allowed (green dots too). It's red only while a disallowed cheat is on. Auto Safe Mode still remembers a cheat used earlier in the attempt.
- Player hitboxes: outer box (red), inner box GD uses against blocks (blue), box turned with the player (pink) and circle (teal), for player 1 and 2 (dual / 2-player).
- Keybinds: right-click any hack or shortcut in the menu and press a key. Backspace removes it, Esc cancels. The key shows next to the hack.
- First batch of QOLMod / Eclipse modules, written from scratch:
  - Level: Freeze Attempts, Instant Complete (cheat), No Camera Shake, Pause Countdown, Coin Tracers, Classic Percentage, Best in Percentage.
  - Player: No Rotation, No Wave Trail, No Trail Blending, No Ghost Trail, Force Ghost Trail, Force Show Player. Noclip can now let you die after N deaths or below an accuracy %.
  - Labels: Level Info (name, creator, ID) and Custom Text with tags ({attempt}, {percent}, {level}, {best}, {jumps}, {cps}, {x}, {mode}...).
  - Shortcuts: Kill Player, Open Settings, Reset Music Volume, Reset SFX Volume.

# v0.7.0

- Click Before Frames now follows a replay. Record a macro with the Replay Bot, pick it under "Replay to follow" in the CBF window (same macros folder). When you click up to N ticks (the stage, F8) before one of the replay's clicks of the same kind (press or release), your click is held back so it acts on exactly the replay's tick. Other clicks go through unchanged. Tested in game: presses 3 to 9 units early were moved +2, +4 and +7 ticks onto the replay's ticks and survived; presses outside the window and late presses were left alone. The hidden-player simulation is no longer used by CBF.
- Log lines keep the same format: Frame0 = your tick, Delayed = the replay's tick.
- New menu look: dark greys, no accent colour, smaller font and tighter rows. Pointercrate dots stay. Window positions are reset once so the narrower windows don't overlap.

# v0.6.0

- New: Smart StartPos (Level tab). Each start position gets the gamemode, speed, size, gravity, mirror and dual the player really has there, worked out from the portals before it. Tested in game: a start position placed as a plain cube after ship, 2x speed, mini and upside-down portals now starts as a mini upside-down ship at 2x.
- Click Before Frames no longer picks the wrong tick: each press is now tested with your own usual hold length for that gamemode (it used to test every press as held down for the whole look-ahead, which made it choose odd delays). Clicks you make while one is held back keep their exact timing (a hold stays a hold).
- New "CBF check" lines in the Geode log: after every save, the real player is compared with the tested path, and any difference is logged. Tested: both saves in the test matched the real player on every tick.
- Show Hitboxes / Hitboxes on Death now show every type: solid blocks, breakable blocks, slopes and coins/collectibles are drawn too (GD's own view skips them), next to hazards, orbs, pads, portals and the player.
- Hitbox Trail only shows while hitboxes are showing (Show Hitboxes on, or after death with Hitboxes on Death).

# v0.5.0

- Click Before Frames rebuilt to work like the clip: every press and release is first tried on a hidden copy of your player. If it would kill you, it is delayed by up to N ticks to the first tick where you survive. Works in every gamemode. Tested in game: presses 50 and 55 units too early before a spike set died with CBF off and survived with it on (+2 and +8 ticks).
- Stages like the clip, F8 cycles them: Off, 2, 4, 8, 16 ticks (wave max 8). Look ahead (how far each click is tested) is in the CBF window, 120 ticks by default.
- Console / CBF-LOGS.txt lines: `*** SAVED +N TICKS (X frames @ 60fps) PRESS! (Frame0: a -> Delayed: b) [Limit: L]`. Frame0 = ticks the click would have survived, Delayed = ticks the delayed click survived.
- Show Hitboxes and Hitboxes on Death now really show. GD 2.2 puts its hitbox drawing in a layer it never draws; the mod now draws it on the level itself. Checked with screenshots taken in game. Hitbox Trail uses the same fix.
- Cheat indicator: Noclip only counts once it actually saves you that attempt, so it stays green if noclip never triggers.
- Noclip Accuracy and Noclip Deaths labels are no longer marked orange.
- The hidden-player simulation (also used by Prediction) now matches the real player exactly (0.000 units error over 140 ticks in the test).

# v0.4.1

- Click Before Frames fixed. It now holds back your early release until GD uses the press (landing, orb), so to GD it looks like you held the button. Tested in game: taps 1-9 ticks early now jump on landing, taps outside the window don't, and holds work as before.
- Click Before Frames no longer waits for Prediction. Prediction only changes the timing once it has proven accurate on that attempt.
- "Count from release" now does something (it was never wired up).
- Pausing while a click is buffered no longer leaves the button stuck down.
- Show Hitboxes and Hitboxes on Death fixed: GD 2.2 only draws hitboxes when its own check says so, and the mod now answers that check. Tested in game.

# v0.4.0

- Labels are now separate modules (Labels tab). Each has its own corner (6 spots), alignment, X/Y offset, size and opacity.
- Prediction (experimental): runs a hidden copy of the player up to 720 ticks ahead to find the ticks where a click saves you. Limited so it costs at most N FPS (10 by default). Click Before Frames aims for those ticks when it's on.
- Frame Counter: shows that click window as boxes (green = works, red = dies), your current tick and where you pressed, plus the prediction's cost and accuracy.
- Replay Bot: record your inputs tick by tick (practice checkpoints supported), play them back, save/load macros (mod folder/macros).
- Best Color: the % turns golden, silver, purple or a custom hex colour while you're past your best.
- Confirm Exit.
- Hitbox Trail: your hitbox on every tick, green when you pressed, blue when you let go.
- Fix Hitboxes On Death not showing.
- Performance tab: Max Performance (one click), Background FPS Limit, Free Textures on Exit.

# v0.3.0

- Click Before Frames rewritten: it now sees GD's jumps, gravity flips and orbs from the player's own state, so it no longer adds clicks you didn't need (this broke late clicks), and works in practice mode (respawns no longer break the tick count). Ticks are real 240/s physics ticks now.
- If you let go before a saved press is applied, the release is moved right after it.
- Console shows only "*** SAVED +N TICKS (X frames @ 60fps) PRESS!/RELEASE!" lines by default. Tick "All logs" to see everything.
- Fix Confirm Restart / Confirm Practice Mode: clicking the button in the popup opened the popup again instead of restarting.
- Best Run: rounds down like GD (or uses Accurate Percentage's decimals) and keeps the run that reached the highest %.
- New: No Shaders, No Portal Lightning, Lock Cursor, Show Taps, and a red flash when noclip saves you (Noclip options).

# v0.2.0

- Fix Click Before Frames never triggering (it only counted clicks released within the window). It now uses GD's own ticks and jump/orb events, also helps held clicks, and logs each decision to the console.
- Click Before Frames: default window is 10 ticks (max 60), shows the window in ms, and can count from release instead of press.
- Menu open/close animation (can be turned off).
- Performance: FPS Bypass no longer caps you at 240 by default, labels only redraw when their text changes, the console only draws visible lines, and hack lookups no longer allocate.
- New hacks: Respawn Delay, No Spider Dash, Auto Kill, Frame Stepper, Auto LDM, Confirm Restart, Confirm Practice Mode, Hide Level Complete VFX, Force Platformer, No Mirror, Instant Mirror, Level Edit, Verify Bypass, No (C) Mark.
- New Shortcuts tab: restart, full restart, toggle practice, place/remove checkpoint, step one tick, show level password, reset level progress, open save/mod folder.

# v0.1.0

- First version: menu, Click Before Frames with CBF-LOGS.txt, Pointercrate cheat indicator, live console, startpos switcher, labels and the first batch of hacks.
