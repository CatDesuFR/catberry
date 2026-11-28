# v1.2.8

- New sound option, "Mute while another app is open": the wallpaper's video sound and the background track go silent whenever a window is open on the desktop, and come back when you minimize everything or show the desktop. Off by default; turn it on under the sound volume. It asks the compositor which windows are open (KDE's KWin and the wlroots compositors, Sway and Hyprland, are supported); on a compositor that doesn't tell, the sound just plays as before.

# v1.2.7

- After an update, the running wallpaper is restarted as the new version (the background track didn't play until logging out).
