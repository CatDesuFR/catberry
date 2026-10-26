// Where roofce keeps its files.
#pragma once

#include <QString>

namespace paths {
// ~/.config/catberry/roofce.conf
QString settingsFile();
// ~/.local/share/catberry/roofce/wallpapers: files the user uploaded.
QString uploads();
// ~/.local/share/catberry/roofce/online: online wallpapers the user picked.
QString downloads();
// ~/.cache/catberry/roofce/thumbs: small previews, safe to delete.
QString thumbs();
// ~/.config/autostart/catberry-roofce.desktop
QString autostartFile();
} // namespace paths
