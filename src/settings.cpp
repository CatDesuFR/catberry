#include "settings.h"

#include "paths.h"

#include <QSettings>

QString placeName(Place place) {
    static const char *const names[kPlaceCount] = {"Top left",    "Top",    "Top right",
                                                   "Left",        "Center", "Right",
                                                   "Bottom left", "Bottom", "Bottom right"};
    return names[int(place)];
}

namespace {
Place readPlace(const QSettings &in, const char *key, Place fallback) {
    int value = in.value(key, int(fallback)).toInt();
    return value >= 0 && value < kPlaceCount ? Place(value) : fallback;
}
} // namespace

Settings Settings::load() {
    QSettings in(paths::settingsFile(), QSettings::IniFormat);
    Settings s;
    s.file = in.value("file").toString();
    s.title = in.value("title").toString();
    s.fit = in.value("fit", s.fit).toString();
    s.sound = in.value("sound", s.sound).toBool();
    s.volume = qBound(0, in.value("volume", s.volume).toInt(), 100);
    s.muteWhenBusy = in.value("mute_when_busy", s.muteWhenBusy).toBool();
    s.autostart = in.value("autostart", s.autostart).toBool();

    s.clock = in.value("clock", s.clock).toBool();
    s.clockPlace = readPlace(in, "clock_place", s.clockPlace);
    s.clockSize = qBound(30, in.value("clock_size", s.clockSize).toInt(), 300);
    s.clockColor = in.value("clock_color", s.clockColor).toString();
    s.clockFont = in.value("clock_font", s.clockFont).toString();
    s.clock24h = in.value("clock_24h", s.clock24h).toBool();
    s.clockSeconds = in.value("clock_seconds", s.clockSeconds).toBool();

    s.music = in.value("music", s.music).toBool();
    s.musicPlace = readPlace(in, "music_place", s.musicPlace);
    s.musicSize = qBound(50, in.value("music_size", s.musicSize).toInt(), 250);
    s.musicColor = in.value("music_color", s.musicColor).toString();
    s.track = in.value("track", s.track).toBool();
    s.trackFile = in.value("track_file").toString();
    s.trackTitle = in.value("track_title").toString();
    s.trackVolume = qBound(0, in.value("track_volume", s.trackVolume).toInt(), 100);
    s.clockPos = in.value("clock_pos", s.clockPos).toPoint();
    s.musicPos = in.value("music_pos", s.musicPos).toPoint();
    s.arrange = in.value("arrange", s.arrange).toBool();
    return s;
}

void Settings::save() const {
    QSettings out(paths::settingsFile(), QSettings::IniFormat);
    out.setValue("file", file);
    out.setValue("title", title);
    out.setValue("fit", fit);
    out.setValue("sound", sound);
    out.setValue("volume", volume);
    out.setValue("mute_when_busy", muteWhenBusy);
    out.setValue("autostart", autostart);

    out.setValue("clock", clock);
    out.setValue("clock_place", int(clockPlace));
    out.setValue("clock_size", clockSize);
    out.setValue("clock_color", clockColor);
    out.setValue("clock_font", clockFont);
    out.setValue("clock_24h", clock24h);
    out.setValue("clock_seconds", clockSeconds);

    out.setValue("music", music);
    out.setValue("music_place", int(musicPlace));
    out.setValue("music_size", musicSize);
    out.setValue("music_color", musicColor);
    out.setValue("track", track);
    out.setValue("track_file", trackFile);
    out.setValue("track_title", trackTitle);
    out.setValue("track_volume", trackVolume);
    out.setValue("arrange", arrange);
    // Positions are left alone: the widgets save them when dragged.
}

void Settings::savePosition(const QString &widget, QPoint pos) {
    QSettings out(paths::settingsFile(), QSettings::IniFormat);
    out.setValue(widget + "_pos", pos);
}

void Settings::clearPosition(const QString &widget) {
    QSettings out(paths::settingsFile(), QSettings::IniFormat);
    out.remove(widget + "_pos");
}
