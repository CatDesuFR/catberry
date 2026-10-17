# catberry

## catberry

One command with three tools, all with raw Xlib windows:

    catberry ac gui        autoclicker: adjustable CPS, left/middle/right click
                           or scroll, rebindable start/stop hotkey (default F6)
    catberry mtools gui    macro recorder: records keys, mouse buttons and wheel
                           with their timing and replays them (no pointer movement)
    catberry ac -v         print a tool's version (also: catberry mtools -v)

Hotkey macros that run in the background:

    catberry -config EasyEdit E,LMB F6 1    save: F6 taps E then LMB, once
    catberry -configload EasyEdit           start listening for the hotkey
    catberry -configunload [EasyEdit]       stop one config, or all of them

`KEYS` is a comma-separated list tapped in order. Names include letters, digits,
`F1`-`F12`, `Space`, `Enter`, `Shift`, `Ctrl`, `Alt`, `LMB`, `RMB`, `MMB`, `MB4`,
`MB5`, `ScrollUp` and `ScrollDown`. Pressing the hotkey again cancels a running
macro. Configs live in `~/.config/catberry/configs/`; each has a `delay_ms` line
(default 15) for how long a key is held and the pause before the next one.

### Build and install on Linux

    sudo dnf install libX11-devel libXtst-devel
    make install        # installs to ~/.local/bin

### Permissions on Linux

Input is sent through XTest on an X11 session and through uinput virtual
devices on Wayland. Seeing keys globally (hotkeys, macro recording, loaded
configs) means reading the keyboards directly, which needs the `input` group:

    sudo usermod -aG input $USER    # then log out and back in

Without it, on Wayland the autoclicker hotkey only fires while an X11
(XWayland) window is focused, and recording and `-configload` do not work.

### Windows 10/11

Same commands, from cmd or PowerShell. Build the Windows package on Linux
with MinGW-w64 (`sudo dnf install mingw64-gcc-c++ mingw64-winpthreads-static`):

    make windows        # dist/catberry-windows.zip

On the Windows PC, unzip it and double-click `install.bat`. It copies
`catberry.exe` to `%LOCALAPPDATA%\Programs\catberry` and adds that folder to
the user PATH (no administrator rights needed); open a new terminal afterwards.
`uninstall.bat` removes it again. Settings, macros and configs live in
`%APPDATA%\catberry`, in the same format as on Linux, so they can be copied
between the two.

Windows needs no extra permissions: hotkeys, recording and configs work out of
the box. It cannot click into windows of programs running as administrator
unless catberry runs as administrator too. The exe is not code-signed, so
SmartScreen may warn on first run ("More info" > "Run anyway").

### Source layout

`src/common` holds everything shared (window logic, clicker, macros, configs),
`src/linux` and `src/windows` the platform parts (window drawing, input
sending and capture, background processes).
