// The chosen wallpaper, how it is shown, and the desktop widgets, shared by
// the window and the wallpaper process (~/.config/catberry/roofce.conf).
#pragma once

#include <QPoint>
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

    // A song (or a video's sound) that plays with the wallpaper, looping.
    // The Music widget shows it and its buttons control it.
    bool track = false;
    QString trackFile;       // roofce's own copy of the imported file
    QString trackTitle;      // the imported file's name, for the window
    int trackVolume = 50;    // 0-100

    // Dragged positions (top-left on the screen); (-1, -1) means "use the
    // Position setting". Written only by the widgets themselves.
    QPoint clockPos{-1, -1};
    QPoint musicPos{-1, -1};
    // While on, the widgets can be dragged (the clock takes the mouse).
    bool arrange = false;

    static void savePosition(const QString &widget, QPoint pos);
    static void clearPosition(const QString &widget);

    static Settings load();
    void save() const;
};
