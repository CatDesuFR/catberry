// A button showing a key (e.g. "F6"); click it, then press a key to change
// it. Esc cancels. Modifier keys are ignored; one "forbidden" key (the
// other hotkey) is refused.
#pragma once

#include "../common/keys.h"

#include <QElapsedTimer>
#include <QPushButton>

class KeyButton : public QPushButton {
    Q_OBJECT
public:
    explicit KeyButton(int code, QWidget *parent = nullptr);
    int code() const { return code_; }
    void setCode(int code);
    void setForbidden(int code) { forbidden_ = code; }
    // Mouse buttons can be chosen too (from global input), e.g. for hotkey macros.
    void setAcceptsMouse(bool on) { acceptsMouse_ = on; }
    bool capturing() const { return capturing_; }
    // Just captured: the same press may still arrive as global input.
    bool recentlyCaptured() const { return captured_.isValid() && captured_.elapsed() < 300; }
    // Global input while capturing (so mouse buttons and keys work without focus).
    void offerGlobal(const InputEvent &ev);
    // A label for when no key is set (code < 0).
    void setEmptyText(const QString &text) { emptyText_ = text; refresh(); }

    // Text before the key name, e.g. "Key: ".
    void setPrefix(const QString &prefix) { prefix_ = prefix; refresh(); }

signals:
    void changed(int code); // a different key was chosen
    void picked(int code);  // a key was chosen, even the same one

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void finish(int code); // -1 cancels
    void refresh();

    int code_;
    int forbidden_ = -1;
    bool acceptsMouse_ = false;
    bool capturing_ = false;
    QElapsedTimer captured_;
    QString emptyText_ = "None";
    QString prefix_;
};

// The evdev code of a Qt key press, or -1.
int evdevCode(const QKeyEvent *event);
