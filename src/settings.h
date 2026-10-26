// The chosen wallpaper and how it is shown, shared by the window and the
// wallpaper process (~/.config/catberry/roofce.conf).
#pragma once

#include <QString>

struct Settings {
    QString file;            // what is shown; empty: no wallpaper chosen
    QString title;
    QString fit = "fill";    // fill, fit, stretch or center
    bool sound = false;      // videos only
    int volume = 50;         // 0-100
    bool autostart = true;   // start the wallpaper when logging in

    static Settings load();
    void save() const;
};
