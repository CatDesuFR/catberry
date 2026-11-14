// The wallpaper itself: a background process that puts one layer-shell
// surface on each screen, in the compositor's background layer, so the
// desktop draws it as the wallpaper instead of a window.
#pragma once

#include <QString>

// `catberry-roofce --wallpaper`: runs until asked to stop.
int runWallpaper(int argc, char **argv);

// For the window and the command line: talk to the running wallpaper.
bool wallpaperRunning();
// Shows the saved wallpaper: reloads a running wallpaper, or starts one.
bool showWallpaper(QString &error);
void stopWallpaper();
// After an update the running wallpaper is still the old program (its file
// was replaced, so it can't know about anything new). If so, stops it and
// starts the new one. True if it restarted it.
bool restartOutdatedWallpaper();
