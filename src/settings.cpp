#include "settings.h"

#include "paths.h"

#include <QSettings>

Settings Settings::load() {
    QSettings in(paths::settingsFile(), QSettings::IniFormat);
    Settings s;
    s.file = in.value("file").toString();
    s.title = in.value("title").toString();
    s.fit = in.value("fit", s.fit).toString();
    s.sound = in.value("sound", s.sound).toBool();
    s.volume = qBound(0, in.value("volume", s.volume).toInt(), 100);
    s.autostart = in.value("autostart", s.autostart).toBool();
    return s;
}

void Settings::save() const {
    QSettings out(paths::settingsFile(), QSettings::IniFormat);
    out.setValue("file", file);
    out.setValue("title", title);
    out.setValue("fit", fit);
    out.setValue("sound", sound);
    out.setValue("volume", volume);
    out.setValue("autostart", autostart);
}
