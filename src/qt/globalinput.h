// Key and mouse-button presses anywhere on the desktop, read from
// /dev/input (needs the input group), delivered on the window's thread.
#pragma once

#include "../common/keys.h"
#include "../linux/linux.h"

#include <QObject>
#include <QTimer>

#include <map>
#include <memory>

class QSocketNotifier;

class GlobalInput : public QObject {
    Q_OBJECT
public:
    explicit GlobalInput(QObject *parent = nullptr);
    ~GlobalInput() override;
    // False when no input device could be opened (not in the input group).
    bool available() const { return evdev_.available(); }

signals:
    void input(InputEvent event);

private:
    void readAll();
    void refreshNotifiers();

    EvdevInput evdev_;
    std::map<int, QSocketNotifier *> notifiers_;
    QTimer rescan_;
};
