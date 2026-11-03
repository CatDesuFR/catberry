// New / edit a hotkey macro config: name, keys, hotkey, count, delay.
#pragma once

#include "../common/macro.h"

#include <QDialog>

class GlobalInput;
class KeyButton;
class QLabel;
class QLineEdit;
class QPushButton;
class NumberEdit;

class ConfigDialog : public QDialog {
public:
    // An empty `name` makes a new config; otherwise the name is fixed.
    ConfigDialog(const QString &name, const MacroConfig &cfg, GlobalInput *input, QWidget *parent = nullptr);
    QString name() const;
    // Valid once the dialog was accepted.
    MacroConfig config() const;

private:
    void check();

    QLineEdit *name_, *keys_;
    QLabel *problem_;
    KeyButton *hotkey_;
    NumberEdit *count_, *delay_;
    QPushButton *ok_;
};
