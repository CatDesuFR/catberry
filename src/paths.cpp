#include "paths.h"

#include <QDir>
#include <QStandardPaths>

namespace {
QString ensure(const QString &dir) {
    QDir().mkpath(dir);
    return dir;
}
QString location(QStandardPaths::StandardLocation type) { return QStandardPaths::writableLocation(type); }
} // namespace

namespace paths {
QString settingsFile() {
    return ensure(location(QStandardPaths::GenericConfigLocation) + "/catberry") + "/roofce.conf";
}
QString uploads() { return ensure(location(QStandardPaths::GenericDataLocation) + "/catberry/roofce/wallpapers"); }
QString downloads() { return ensure(location(QStandardPaths::GenericDataLocation) + "/catberry/roofce/online"); }
QString tracks() { return ensure(location(QStandardPaths::GenericDataLocation) + "/catberry/roofce/track"); }
QString thumbs() { return ensure(location(QStandardPaths::GenericCacheLocation) + "/catberry/roofce/thumbs"); }
QString autostartFile() {
    return ensure(location(QStandardPaths::GenericConfigLocation) + "/autostart") + "/catberry-roofce.desktop";
}
} // namespace paths
