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
| MF  | Misc Fixes | Screen share sound on Linux works: Vesktop's audio share is set up for you, and put back as it was when you unload. Search results (Ctrl+F) show one line per result when it fits, and replies show the message they answer instead of "Message could not be loaded". Avatars and banners in profiles open full size when clicked (Vencord's ViewIcons, switched off again on unload if it was off), and right-clicking an avatar, banner or server icon there gives **Download (full quality)**: the original picture, as big as Discord stores it, saved to Downloads. |
| TS  | Theme Stealer | Right-click a server: **Save Discord's theme...** copies everything Discord looks like right now (its colours, a Nitro gradient theme, every Vencord theme you have on, your QuickCSS and Discord's App Icon), or **Steal server look...** copies a server's colours and icon. Name it, and it is saved as a theme and switched on. While a theme with an icon is on, the icon shows on the home button, the taskbar and the tray. Also `/stealtheme`. |
| ACA | All Characters In Channel/Category Names | Capital letters and symbols in text channel names, typed straight into Discord's Edit Channel box (or right-click a channel: **Rename (all characters)...**). They are written with look-alike characters, so they read as normal. Discord's servers remove every kind of blank space, so a space shows as a middle dot (`Off·Topic`; underscore, bar or dash in the plugin's settings). Needs Manage Channels. |
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
