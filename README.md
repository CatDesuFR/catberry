# catberry scan

A **PC check for cheats** (version 1.0.1), for Windows 10/11. Someone asks a
player to run it, and the player sends back the results file. It looks for:

- **Roblox** cheats (`catberry scan roblox`): executors like Solara, Wave, Xeno,
  KRNL, Fluxus and others, and any executor folder (one with `autoexec` and
  `workspace` script folders, whatever it is called).
- **Counter-Strike 2** cheats (`catberry scan cs2`): Aimware, Onetap,
  Neverlose, Gamesense and others, `-insecure` in the CS2 launch options.
- **Easy Anti-Cheat** game cheats (`catberry scan ezac`): EAC bypasses and HWID
  spoofers, for every EAC game it finds in Steam and Epic Games.

Every scan also looks for cheat tools (Cheat Engine, DLL injectors, kdmapper,
ReClass and others) and for **memory injection traces** in every running
program: programs loaded into memory by hand instead of from a file, program
images whose file is gone, and threads that start in memory no file backs.
These are listed as **UNIDENTIFIED**: code running the way injected cheats run,
not tied to a known cheat.

This branch holds only the scanner. The `catberry` command it runs under lives
on the [`main` branch](https://github.com/CatDesuFR/catberry). **Install the
`catberry` launcher first**, then this.

## Install

```bat
catberry install scan
```

Remove it with `catberry uninstall scan`. Every download is on the
[Releases](https://github.com/CatDesuFR/catberry/releases) page (tags `scan-v...`).

## Use

1. **Close the game.** The scan never looks inside the game itself: reading a
   protected game's memory looks like cheating to its anti-cheat, so it refuses
   to start while the game runs.
2. Open a terminal **as administrator** (right-click Terminal or PowerShell, Run
   as administrator). Without it, the checks of programs run before (prefetch,
   BAM) and of other users' programs are skipped, and the report says so.
3. Run the scan for the game:

   ```bat
   catberry scan roblox
   ```

It writes `results_scan_<scan id>.txt` in the current folder (or in Documents
if it can't write there) and prints where.

## What it checks

| Where | What |
| --- | --- |
| Running programs | names, loaded DLLs, executable memory, thread start addresses |
| Drivers | cheat and cheat-tool drivers (kdmapper's, Cheat Engine's...), drivers loaded from user or temp folders |
| Boot options | Windows test mode, driver signature checks off |
| Programs run before | prefetch and BAM (administrator), MuiCache and UserAssist |
| Game folders | unsigned DLLs Windows would load into the game (`dxgi.dll`, `version.dll`...), Roblox FFlag edits, CS2 `-insecure` |
| Files | desktop, downloads, documents, app data, temp, program files, other drives' top folders |

Results are marked **CHEAT** (a known cheat for the game), **CHEAT TOOL** (a
tool for cheating in any game), **UNIDENTIFIED** (injection trace) or
**WARNING** (a setting or file cheats use, which also has honest uses, like
ReShade's `dxgi.dll`). The report also lists what was checked and what could
not be.

## Checking a report wasn't edited

The last line of each report is a signature over everything above it. Whoever
receives the report checks it with:

```bat
catberry scan verify results_scan_20261127-221530-a1b2c3.txt
```

It says **UNCHANGED** for the report as the scan wrote it, and **CHANGED** if
anything in it was edited (one letter is enough). The file is also locked
against other programs writing to it while the scan runs.

The signing key is given to the official release when it is built and is not
in this source code, so a self-built copy marks its reports **UNOFFICIAL** and
can't make reports the official one accepts. The key is kept scrambled inside
the program, but a determined person could still dig it out of it: treat the
signature as strong evidence, not proof.

## Build it yourself

Needs MinGW-w64 (`g++-mingw-w64-x86-64` on Debian/Ubuntu, `mingw-w64-gcc` on
Arch):

```bash
make windows WINCXX=x86_64-w64-mingw32-g++-posix
```

That gives `dist/catberry-scan-windows.zip` (an unofficial build, see above).
