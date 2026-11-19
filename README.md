# catberry vskm

Vencord plugins for [Vesktop](https://github.com/Vencord/Vesktop), switched on
and off from the terminal. Linux only.

```
catberry install vskm
catberry vskm -list            # the plugins, and which are loaded
catberry vskm load NAME        # switch a plugin on
catberry vskm unload NAME      # switch it off
catberry vskm -v               # version
```

If Vesktop is open, it is closed and started again so the change takes effect.
Unloading a plugin puts back what it changed (Vesktop's audio share settings,
Discord's GIF and emoji settings). `catberry vskm unload all` switches them all
off, and is run for you by `catberry uninstall vskm`.

## Plugins

| Name | Plugin | What it does |
|------|--------|--------------|
| MF  | Misc Fixes | Screen share sound on Linux works: Vesktop's audio share is set up for you, and put back as it was when you unload. |
| TS  | Theme Stealer | Right-click a server: **Steal server look...** or **Save Discord's theme...**, give it a name, and it is saved as a theme in Vesktop's themes folder and switched on. Also `/stealtheme`. |
| ACA | All Characters In Channel/Category Names | Right-click a channel or category: **Rename (all characters)...** lets the name have capital letters, spaces and symbols. They are written with look-alike characters, so they read as normal. Needs Manage Channels. |
| OD  | OptiDiscord | Makes Discord lighter: frees messages of channels you left, does no work while Vesktop is hidden, and stops GIFs, emoji and stickers moving. Each option can be switched in the plugin's settings; "Fewer animations" is off by default. |

Plugin settings are in Vesktop under Settings → Vencord → Plugins.

## How it works

catberry vskm comes with its own build of Vencord with these plugins in it
(`vencord/`, made by `scripts/build-vencord.sh` from the Vencord commit in
`VENCORD_COMMIT`). Loading a plugin points Vesktop at that build (its
`vencordDir` setting) and switches the plugin on; unloading the last one puts
back the Vencord Vesktop used before. All your other Vencord plugins and
settings stay as they are.

## Building

```
scripts/build-vencord.sh   # needs git and node; makes vencord/
make                       # makes catberry-vskm
```
