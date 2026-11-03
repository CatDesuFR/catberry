// New / edit a quick-click set: name, hotkey, gap and its points.
#pragma once

#include "../common/quickclick.h"

#include <QDialog>

class GlobalInput;
class KeyButton;
class NumberEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class QuickDialog : public QDialog {
public:
    // An empty `name` makes a new set; otherwise the name is fixed.
    QuickDialog(const QString &name, const QuickSet &set, GlobalInput *input, QWidget *parent = nullptr);
    QString name() const;
    QuickSet set() const { return set_; }

private:
    void pick();
    void fillTable();
    void move(int delta);
    void removePoint();
    void check();

    QuickSet set_;
    QLineEdit *name_;
    KeyButton *hotkey_;
    NumberEdit *gap_;
    QTableWidget *points_;
    QPushButton *up_, *down_, *remove_, *pick_, *ok_;
    QLabel *problem_;
};
