# catberry

An autoclicker, a macro recorder and background hotkey macros behind one
`catberry` command, for **Linux** and **Windows 10/11**. The commands are the
same on both.

- [Install on Windows](#install-on-windows)
- [Install on Linux](#install-on-linux)
- [Usage](#usage)
- [Uninstall](#uninstall)
- [Building from source](#building-from-source)

---

## Install on Windows

1. Download **[`catberry-windows.zip`](https://github.com/CatDesuFR/catberry/raw/main/release/v1.0.2/catberry-windows.zip)**
   (starts the download), or get it from the
   [Releases page](https://github.com/CatDesuFR/catberry/releases).
2. Right-click the zip → **Extract All…**, and open the extracted
   `catberry-windows` folder.
3. Double-click **`install.bat`**.
   - If Windows shows "Windows protected your PC", click **More info** →
     **Run anyway**. The program is not code-signed, so this is expected.
   - No administrator rights are needed. It copies `catberry.exe` to
     `%LOCALAPPDATA%\Programs\catberry` and adds that folder to your PATH.
4. Close any open terminals, then open a **new** Command Prompt or PowerShell
   and check it works:

   ```
   catberry ac -v
   ```

   It should print `catberry ac 1.0.2`.

Nothing else to set up: hotkeys, macro recording and background configs work
straight away on Windows.

**If `catberry` is "not recognized"**, the PATH step did not take effect. Sign
out of Windows and back in, or run it by its full path:
`%LOCALAPPDATA%\Programs\catberry\catberry.exe ac gui`.

**If antivirus deletes or blocks it**: catberry watches the keyboard for its
hotkeys and sends clicks, which some antivirus software flags. Allow it in your
antivirus if you trust it.

---

## Install on Linux

### Option A: download the program

Works on recent distros (glibc 2.38 or newer: Fedora 39+, Ubuntu 24.04+,
Debian 13+, Arch, Linux Mint 22+). On older ones use option B.

1. Download **[`catberry-linux-x86_64`](https://github.com/CatDesuFR/catberry/raw/main/release/v1.0.2/catberry-linux-x86_64)**
   (starts the download), or get it from the
   [Releases page](https://github.com/CatDesuFR/catberry/releases).
2. Install it as `catberry` in `~/.local/bin`:

   ```bash
   install -Dm755 ~/Downloads/catberry-linux-x86_64 ~/.local/bin/catberry
   ```

3. Check it works:

   ```bash
   catberry ac -v
   ```

   If it says "command not found", `~/.local/bin` is not on your PATH. Add it
   and open a new terminal:

   ```bash
   echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
   ```

### Option B: build it from source

Install a compiler and the X11 libraries:

| Distro | Command |
| --- | --- |
| Fedora | `sudo dnf install gcc-c++ make git libX11-devel libXtst-devel` |
| Ubuntu / Debian / Mint | `sudo apt install g++ make git libx11-dev libxtst-dev` |
| Arch | `sudo pacman -S --needed base-devel git libx11 libxtst` |

Then build and install to `~/.local/bin`:

```bash
git clone https://github.com/CatDesuFR/catberry.git
cd catberry
make install
```

### Allow global hotkeys and recording (once)

catberry reads the keyboard directly to see hotkeys while other windows are
focused. That needs your user in the `input` group:

```bash
sudo usermod -aG input $USER
```

Then **log out and back in** (a new terminal is not enough).

Without this, on Wayland (the default on Fedora, Ubuntu and KDE) the hotkeys
only work while an X11 window is focused, and macro recording and
`-configload` are disabled. The windows show a red note while this is missing.
Note that members of `input` can read all keyboard input.

---

## Usage

```
catberry ac gui                 open the autoclicker
catberry ac -v                  print the autoclicker version
catberry mtools gui             open the macro recorder
catberry mtools -v              print the macro recorder version
catberry -config NAME KEYS HOTKEY COUNT
                                save a hotkey macro
catberry -configload NAME       start a saved macro in the background
catberry -configunload [NAME]   stop one loaded macro, or all of them
```

### Autoclicker (`catberry ac gui`)

Set the clicks per second, choose left, middle or right click or scroll up or
down, and start or stop with the hotkey (default **F6**, click the hotkey
button to change it). The **Start** button waits 3 seconds so you can move the
mouse away; the hotkey starts instantly. While it runs, clicking anywhere on
its window stops it.

The **Click test** box measures how many clicks actually arrive: start the
clicker and hover over the box.

### Macro recorder (`catberry mtools gui`)

Records keyboard keys, mouse buttons and the scroll wheel with their timing,
then plays them back (mouse movement is not recorded).

- **Record** / **F8**: start and stop recording.
- **Play** / **F9**: play it back. **Repeat** sets how many times (0 = until
  stopped).
- Type a name and press **Save** or **Load** to keep macros.

### Hotkey macros (`catberry -config`)

```bash
catberry -config EasyEdit E,LMB F6 1   # F6 taps E, then left click, once
catberry -configload EasyEdit          # start it in the background
catberry -configunload EasyEdit        # stop it (no name: stop all)
```

- `KEYS` is a comma-separated list, pressed one after another.
- `COUNT` is how many times the list runs per hotkey press. Pressing the
  hotkey again while it runs cancels it.
- Several configs can be loaded at once.
- Key names: letters, digits, `F1`-`F12`, `Space`, `Enter`, `Tab`, `Esc`,
  `Backspace`, `Shift`, `Ctrl`, `Alt`, `Super`, `Up`/`Down`/`Left`/`Right`,
  `Insert`, `Delete`, `Home`, `End`, `PageUp`, `PageDown`, `KP0`-`KP9`, and the
  mouse: `LMB`, `RMB`, `MMB`, `MB4`, `MB5`, `ScrollUp`, `ScrollDown`.
- Each config has a `delay_ms` line (default 15) for how long each key is held
  and the pause before the next one. Raise it if a game misses keys.

### Where settings are kept

| | Linux | Windows |
| --- | --- | --- |
| Settings, macros, configs | `~/.config/catberry/` | `%APPDATA%\catberry\` |

The files are the same format on both, so configs and macros can be copied
between Linux and Windows.

### Good to know

- The hotkeys are not blocked: the game or app also sees the key.
- On Windows, catberry cannot click into programs running as administrator
  unless it is also run as administrator.
- Autoclickers and macros are against the rules of many online games, and
  catberry does nothing to hide itself. Use it at your own risk.

---

## Uninstall

**Windows:** run `uninstall.bat` from the extracted zip. It stops loaded
configs, removes the program and its PATH entry. Your settings stay in
`%APPDATA%\catberry`; delete that folder to remove them too.

**Linux:**

```bash
catberry -configunload
rm ~/.local/bin/catberry
rm -r ~/.config/catberry        # optional: settings, macros and configs
```

---

## Building from source

`src/common` holds everything shared (window logic, clicker, macros,
configs). `src/linux` and `src/windows` hold the platform parts: window
drawing, sending and watching input, and background processes.

| Target | Command | Output |
| --- | --- | --- |
| Linux program | `make` | `catberry` |
| Install on Linux | `make install` | `~/.local/bin/catberry` |
| Windows package | `make windows` | `dist/catberry-windows.zip` |

Ready-built downloads for each version are kept in `release/<version>/`, with
a `SHA256SUMS` file to check them against.

The Windows package is cross-compiled on Linux with MinGW-w64
(Fedora: `sudo dnf install mingw64-gcc-c++ mingw64-winpthreads-static`).
