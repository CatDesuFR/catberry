#include "keybutton.h"

#include <QKeyEvent>
#include <QStyle>

int evdevCode(const QKeyEvent *event) {
    // On Linux (Wayland and X11) the native scan code is the XKB keycode,
    // which is the evdev code plus 8.
    quint32 native = event->nativeScanCode();
    return native > 8 ? int(native - 8) : -1;
}

namespace {
bool isModifier(int code) {
    switch (code) {
    case 29: case 97:   // Ctrl
    case 42: case 54:   // Shift
    case 56: case 100:  // Alt
    case 125: case 126: // Super
    case 58: case 69:   // CapsLock, NumLock
        return true;
    default: return false;
    }
}
int g_capturing = 0;        // key buttons waiting for a key, app-wide
QElapsedTimer g_lastCapture; // when any of them last got one
} // namespace

bool KeyButton::anyBusy() { return g_capturing > 0 || (g_lastCapture.isValid() && g_lastCapture.elapsed() < 300); }

KeyButton::~KeyButton() { setCapturing(false); }

void KeyButton::setCapturing(bool on) {
    if (on == capturing_) return;
    capturing_ = on;
    g_capturing += on ? 1 : -1;
}

KeyButton::KeyButton(int code, QWidget *parent) : QPushButton(parent), code_(code) {
    setFocusPolicy(Qt::StrongFocus);
    refresh();
    connect(this, &QPushButton::clicked, this, [this] {
        setCapturing(!capturing_);
        if (capturing_) setFocus();
        refresh();
    });
}

void KeyButton::setCode(int code) {
    code_ = code;
    refresh();
}

void KeyButton::refresh() {
    setObjectName(capturing_ ? "accent" : "");
    setText(capturing_ ? "Press a key..." : code_ >= 0 ? prefix_ + QString::fromStdString(keyName(code_)) : emptyText_);
    style()->unpolish(this);
    style()->polish(this);
}

void KeyButton::finish(int code) {
    setCapturing(false);
    if (code >= 0 && code != code_) {
        code_ = code;
        emit changed(code);
    }
    if (code >= 0) emit picked(code);
    captured_.start();
    g_lastCapture.start();
    refresh();
}

void KeyButton::keyPressEvent(QKeyEvent *event) {
    // The press that was just captured (it may arrive here after global
    // input already took it): swallowed, so Space/Enter do not click this
    // button again and Esc does not close a dialog.
    if (!capturing_ && recentlyCaptured()) return event->accept();
    if (!capturing_) return QPushButton::keyPressEvent(event);
    if (event->isAutoRepeat()) return;
    if (event->key() == Qt::Key_Escape) return finish(-1);
    int code = evdevCode(event);
    if (code < 0 || isModifier(code) || code == forbidden_) return;
    finish(code);
}

void KeyButton::offerGlobal(const InputEvent &ev) {
    if (!capturing_ || !ev.down) return;
    bool mouse = ev.code >= keycode::MouseLeft && ev.code <= keycode::MouseForward;
    // The left click that pressed this button must not become the key.
    if (mouse && (!acceptsMouse_ || ev.code == keycode::MouseLeft)) return;
    if (ev.code == keycode::Escape) return finish(-1);
    if (isWheel(ev.code) || isModifier(ev.code) || ev.code == forbidden_) return;
    finish(ev.code);
}

void KeyButton::keyReleaseEvent(QKeyEvent *event) {
    if (capturing_ || recentlyCaptured()) return event->accept();
    QPushButton::keyReleaseEvent(event);
}

void KeyButton::focusOutEvent(QFocusEvent *event) {
    if (capturing_) {
        setCapturing(false);
        refresh();
    }
    QPushButton::focusOutEvent(event);
}
