# v1.1.4

- Frame Window Counter: clicks are no longer lost in spam sections. At a big accuracy (like 4096) each click takes long to measure, and only 16 could wait at a time, so new clicks were thrown away with no marker; now 64 can wait, and the counter gets more time per frame while clicks pile up (Clairvoyaance: 3 clicks lost before, none now).
- Frame Window Counter: a press while already holding, or a release that ends nothing (a second key, letting go twice), was counted as another click, so the tally was off (Clairvoyaance's wave counted 4 releases in a row). Only real clicks count now.
- Frame Window Counter: a click tried later than it really was is now replayed and must be survived too; before, past the end of the re-run it was left out, so late tries looked fine and windows came out too big.
- Frame Window Counter: Precision, like NaN GD (NaNDL L*): the precision you need to beat what you've played within 24 hours of tries, from every measured click's window and time. It goes up with every click. Shown under the tally ("Prec"); options for it and for the respawn time it counts with.
- Slider Limit Bypass (Global): editor and settings sliders go past both ends.
- Enter Plays the Playtest (Creator): in the editor, Enter starts the playtest, pauses it and carries on.
- Show Hitboxes and Noclip now work in editor playtests too.
- Jump Keys (Player): add keys that jump, remove them again, and turn off Space, W or Up, in levels and playtests.

# v1.1.3

- Replay Bot: regular position fixes. While recording, the players' state is kept every 30 ticks (8 times a second) too, not only at clicks, and playback puts them back there. Before, a long stretch with no clicks could drift far from the recording (on Poltergeist 2 the wave drifted over 100 units between two clicks and died at 19.8%), and so could the run after a practice respawn. Record your macros again to get them.
- Replay Bot: practice respawns. Respawning at a checkpoint while recording keeps the player's state right then as a fix, and if a button you held when you died isn't held after the respawn (its release was after the checkpoint, so it was dropped with everything else), the macro lets go too. Before, the bot could keep holding where you weren't.
- Frame Window Counter: releases only count as wave, ship or robot (where letting go matters); other releases still move with their press. Its re-runs follow the bot's regular fixes too. The mouse cursor shows on the loading screen.

# v1.1.2

- Replay Bot: clicks made with GD's second jump key (the "player 2" key, which also moves player 1 in one-player levels) were recorded as player 2's, so they never got their real tick or their position fix: most of a macro played uncorrected and drifted. In one-player levels both keys now count as player 1's everywhere, and fixes keep both players in line in dual mode and two-player levels. Tested with the Up key through GD's keyboard handler: 159 of 159 clicks fixed, every click on the right tick, drift 0.001 units on average.
- Frame Window Counter: it ignored clicks made with the second jump key, so its re-runs left most of your clicks out and came out as "?". They count now: with second-key clicks, the loading screen measured 14 of 14, none unsure. New option "Hide unsure (?) clicks" (on): a click that can't be measured gets no marker at all.
- Speedhack music: with the speedhack's "Speedhack music" option, the song plays at the speedhack's speed, also after restarting or changing level (before, new music started at normal speed). Tested: 2x speedhack, music at 2.00x.
- Menu key: change the key that opens the menu (Menu window: "Menu key", Change, press a key; Backspace puts it back to Tab).
- CPS label: CPS / most CPS this attempt / total clicks, e.g. "CPS: 7/12/230" ({maxcps} tag for custom labels).
- No Orb Pulse: also holds GD's audio scale at the source every frame and FMOD's pulse values while GD sizes objects.

# v1.1.1

- Replay Bot: accurate with real clicks. A real click (keyboard or mouse) acts partway through its physics tick, at the moment its timestamp says, but a bot can only hand clicks over at the start of a tick. Each click's position fix is now taken at the end of its tick and put back at the end of the same tick when playing, so the runs line up from there on. Tested with clicks made through GD's own keyboard handler: drift went from 0.074 to 0.001 units on average, every click on the right tick, also at 144 FPS.
- Frame Window Counter: the loading screen measures every click before you play. It no longer skips clicks when many wait at once (it waits for the measuring to catch up), re-runs follow the bot's position fixes, and releases start from their own tick, so clicks no longer come out as "?". Tested: 14 of 14 test clicks measured, none unsure. If the macro dies while measuring (wrong level or an old recording), the loading screen says where and asks "Play anyway" or "Cancel" instead of starting with clicks missing; Cancel no longer starts the loading screen again.
- End Screen Stats: on its own line under GD's stats (it covered the buttons), with your clicks and the real time of the attempt (it showed GD's jump count, 0 with the bot).
- No End Shake: the level complete screen appears in place instead of bouncing in.
- New: Pause Zoom (on by default). While paused, scroll to zoom into the level around the mouse; hitboxes zoom with it. The pause menu hides while zoomed in; scroll back out (or resume) to get it back.
- New: No Pad Signal. Touching or using an orb, or using a pad, no longer sets off the level's own effects for it (levels use these to make orbs pulse or the screen flash), and pads and orbs don't flash. If orbs still pulse on a level with No Orb Pulse on, this is what stops it.
- New: No Gravity Signal. Flipping gravity no longer sets off the level's effects for it, and GD's arrows don't show. Tested on Deadlocked: 37 signals and 6 arrow effects with both off, none with both on.
- Best Run: follows the furthest you've got this session live, and a finished level counts as 100% (it showed 99.56%).
- Hitbox Trail no longer turns the cheat indicator red.

# v1.1.0

- Replay Bot is accurate now, the way Eclipse's bot is. Every click is saved with the tick it really reached the player on (before, it was saved with the tick it came in on and played back one tick late, so every click landed off and runs drifted until they died), and with where the player was at the start of that tick: position, rotation and speed. Playback puts the player back exactly there before giving it the click, so tiny differences can't add up. Tested in game on Deadlocked with 159 clicks: every click on the same tick and no drift at all (0.0000 units), also at 144 FPS; played the old way, 0 of 159 clicks were on the right tick and the player drifted up to 3.4 units. New macro format (v2); old macros still play, but record them again for full accuracy (the bot window says so).
- Frame Window Counter: accuracy goes up to 4096 ticks each way (32, 64, 128 ... 4096). Up to 64 ticks a click is moved one tick at a time as before; past that the search doubles its step and then narrows down to the exact tick, and the kept history is spread out so it always reaches back far enough. Clicks near the start of a level are measured from the level start. Tested: at 4096 the loading screen measured all 14 test clicks in about a second, presses gave the same windows as at 64 (11 ticks), and releases that can be held forever now show 4096 instead of stopping at 64.
- Frame Window Counter: releases are circles too (hollow, so they read apart from presses), no more diamonds.
- New: No Orb Pulse. Orbs and pads stop pulsing to the music and stay one size (option: the size, 0.75 = normal). Tested on Polargeist: orbs that pulsed between 0.4 and 1.2 stay exactly one size.
- Fix: the cheat indicator went back to green when you turned noclip off after it saved you. It now stays red for the rest of the attempt once a disallowed hack was used, and goes green when you restart. Tested in game.
- Fix: Hitboxes On Death kept the hitboxes switched on after you respawned in practice mode, so every death drew on top of the ones before. Each respawn now puts them back how they were. Tested: after each respawn the hitbox layer is empty again.
- Performance: measured on your setup (RTX 4080 SUPER, 240 Hz): the mod's features cost about 0.02 ms per frame (0.16 ms with them, 0.14 ms without), and GD could run at over 6000 FPS. The drops come from VSync: with Vertical Sync on, the 1% lows fall to 140-200 FPS; with it off they stay at about 225. The 240 FPS cap is your desktop (KWin) syncing windows to the monitor.

# v1.0.0

- Frame Window Counter: sub-tick precision. After finding the whole ticks a click can move, both edges of its window are narrowed down to 1/64 of a tick on the hidden player (the tick the edge falls in is split in two), so windows show with a decimal, e.g. "5.3". The colour band uses the rounded value. Decimals assume the click can land between ticks (as with Click Between Frames style input); with plain per-tick input the whole number is what counts.
- Frame Window Counter: releases are measured and counted too (diamond markers; a release can't move before its press). The tally shows presses and releases separately.
- Frame Window Counter: new "ms" row in the top-left list with the last click's window in milliseconds (one tick at 240 TPS = 4.17 ms), its window in frames, and whether it was a press or a release.
- The loading screen measures the macro's releases as well.

# v0.13.1

- Fix: finishing a level with Hide Level Complete VFX on (also turned on by Max Performance) left you stuck without an end screen. GD's complete effect now always runs and only its effects are hidden. Tested in game.
- Fix: keybinds couldn't be set (the menu never gets right-clicks). Every hack and shortcut now has an options arrow with "Keybind: Set / Clear" and "Favourite". Tested in game: a bound key toggles its hack.
- Frame Window Counter: its tally now replaces the labels in the top-left corner while it's on, counts windows over 10 too ("10+"), and shows how many clicks were worked out.
- Frame Window Counter: a ding when you pass each click (made in code, no sound files; tighter windows ring higher; on/off and volume in its options). Bolder rings with a tinted fill and a soft glow, more vivid band colours (windows over 10 are purple), bigger numbers that pop in.
- Frame Window Counter: the tally and the markers now live on GD's HUD layer, so camera moves can't push the list off the corner (it was cut off on the left); markers follow the level and scale with camera zoom, and have a dark outline under the coloured ring so they read on any background. The tally has a name column and a count column.
- Frame Window Counter now measures the clicks that actually happen (yours, the ones CBF hands to GD, and the Replay Bot's), so it works without a macro. When a press comes in, the player's state from just before it is kept; once the next press comes, that stretch is re-run on the hidden player with the press (and its release) moved earlier and later and every other click where it really was. A grey placeholder ring shows at the press until it's measured. If the press where it really was can't be reproduced (or really died), it shows "?" instead of a made-up number. Measuring runs a little each frame, so clicks don't stutter. Tested: plain presses on the test level measured 11 ticks, the same as the replay analyser, at 232 FPS (240 cap).
- Frame Window Counter: loading screen. Entering a level with the Replay Bot on Play, a macro for this level and the counter on: a loading screen ("Measuring frame windows", progress bar, clicks done / total, Cancel) fast-forwards the macro in the background with the music paused and measures every press, then starts the level for real; each ring appears the moment its click happens. Accuracy setting in the counter's options: Max (64 ticks each way, default) or Balanced (32). Can be turned off ("Measure the bot's macro first"). A press with no next press has to survive half a second. Tested: 7 presses measured in under a second, then shown instantly during the real run.
- Macros now remember their level on every recording. Click Before Frames won't follow a replay recorded on another level, and the counter's list says which level it belongs to. (Older macros without a saved level can't be checked.)
- Replay Bot: playback gives each click to GD on exactly the tick it was recorded on (it could be a tick early before); clicks Click Before Frames hands to GD are recorded on the right tick. The loaded macro is shown clearly; a Delete button (click twice); finishing a level in practice mode while recording saves the macro automatically (name box, or "<level> <date time>"). Tested in game.

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
