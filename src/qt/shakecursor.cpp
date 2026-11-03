// KDE: KWin's effects, over D-Bus. A marker file says the effect was switched
// off by us, so a crashed run cannot leave it off for good.
#include "../common/shakecursor.h"

#include "../common/platform.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <system_error>

namespace {

const char *const kEffect = "shakecursor";

fs::path marker() { return runtimeDir() / "shakecursor-paused"; }

bool onKde() {
    const char *desktop = std::getenv("XDG_CURRENT_DESKTOP");
    return desktop && std::strstr(desktop, "KDE");
}

QDBusInterface *effects() {
    // QtDBus wants an application object; the background process has none.
    static int argc = 1;
    static char name[] = "catberry-mtools";
    static char *argv[] = {name, nullptr};
    if (!QCoreApplication::instance()) new QCoreApplication(argc, argv);
    static QDBusInterface iface("org.kde.KWin", "/Effects", "org.kde.kwin.Effects", QDBusConnection::sessionBus());
    return iface.isValid() ? &iface : nullptr;
}

bool loaded(QDBusInterface *e) {
    QDBusReply<bool> reply = e->call("isEffectLoaded", kEffect);
    return reply.isValid() && reply.value();
}

} // namespace

ShakeCursorPause::ShakeCursorPause() {
    std::error_code ec;
    if (!fs::exists(marker(), ec)) return;
    if (QDBusInterface *e = onKde() ? effects() : nullptr) e->call("loadEffect", kEffect);
    fs::remove(marker(), ec);
}

ShakeCursorPause::~ShakeCursorPause() {
    if (paused_) resume();
}

void ShakeCursorPause::pause() {
    if (paused_ || !onKde()) return;
    QDBusInterface *e = effects();
    if (!e || !loaded(e)) return; // off already, or not KWin: nothing to do
    std::ofstream(marker()) << "1\n";
    e->call("unloadEffect", kEffect);
    paused_ = true;
}

void ShakeCursorPause::resume() {
    if (!paused_) return;
    if (QDBusInterface *e = effects()) e->call("loadEffect", kEffect);
    std::error_code ec;
    fs::remove(marker(), ec);
    paused_ = false;
}
