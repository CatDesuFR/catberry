// The chosen wallpaper, how it is shown, and the desktop widgets, shared by
// the window and the wallpaper process (~/.config/catberry/roofce.conf).
#pragma once

#include <QString>

// Where a desktop widget sits on the screen.
enum class Place { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };
const int kPlaceCount = 9;
QString placeName(Place place);

struct Settings {
    QString file;            // what is shown; empty: no wallpaper chosen
    QString title;
    QString fit = "fill";    // fill, fit, stretch or center
    bool sound = false;      // videos only
    int volume = 50;         // 0-100
    bool autostart = true;   // start the wallpaper when logging in

    // Mond-style clock: day name, date and time.
    bool clock = true;
    Place clockPlace = Place::Center;
    int clockSize = 100;     // percent
    QString clockColor = "#ffffff";
    QString clockFont;       // empty: the best available thin font
    bool clock24h = true;
    bool clockSeconds = false;

    // What is playing in Spotify (or another player).
    bool music = true;
    Place musicPlace = Place::BottomLeft;
    int musicSize = 100;     // percent
    QString musicColor = "#ffffff";

    static Settings load();
    void save() const;
};
