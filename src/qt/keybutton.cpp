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
} // namespace

KeyButton::KeyButton(int code, QWidget *parent) : QPushButton(parent), code_(code) {
    setFocusPolicy(Qt::StrongFocus);
    refresh();
    connect(this, &QPushButton::clicked, this, [this] {
        capturing_ = !capturing_;
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
    setText(capturing_ ? "Press a key..." : code_ >= 0 ? QString::fromStdString(keyName(code_)) : emptyText_);
    style()->unpolish(this);
    style()->polish(this);
}

void KeyButton::finish(int code) {
    capturing_ = false;
    if (code >= 0 && code != code_) {
        code_ = code;
        emit changed(code);
    }
    captured_.start();
    refresh();
}

void KeyButton::keyPressEvent(QKeyEvent *event) {
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

void KeyButton::focusOutEvent(QFocusEvent *event) {
    if (capturing_) {
        capturing_ = false;
        refresh();
    }
    QPushButton::focusOutEvent(event);
}
