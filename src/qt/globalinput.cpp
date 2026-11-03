#include "globalinput.h"

#include <QSocketNotifier>

#include <set>

GlobalInput::GlobalInput(QObject *parent) : QObject(parent) {
    qRegisterMetaType<InputEvent>("InputEvent");
    refreshNotifiers();
    // Keyboards and mice plugged in later.
    connect(&rescan_, &QTimer::timeout, this, [this] {
        evdev_.rescan();
        refreshNotifiers();
    });
    rescan_.start(2000);
}

GlobalInput::~GlobalInput() {
    for (auto &entry : notifiers_) delete entry.second;
}

void GlobalInput::readAll() {
    for (const InputEvent &ev : evdev_.read()) emit input(ev);
    refreshNotifiers(); // a device may have gone
}

void GlobalInput::refreshNotifiers() {
    std::vector<pollfd> fds;
    evdev_.addPollFds(fds);
    std::set<int> open;
    for (const pollfd &p : fds) open.insert(p.fd);
    for (auto it = notifiers_.begin(); it != notifiers_.end();) {
        if (open.count(it->first)) {
            ++it;
        } else {
            delete it->second;
            it = notifiers_.erase(it);
        }
    }
    for (int fd : open) {
        if (notifiers_.count(fd)) continue;
        auto *n = new QSocketNotifier(fd, QSocketNotifier::Read);
        connect(n, &QSocketNotifier::activated, this, [this] { readAll(); });
        notifiers_[fd] = n;
    }
}
