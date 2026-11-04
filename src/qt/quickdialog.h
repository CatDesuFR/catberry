// New / edit a quick-click set: name, hotkey, gap and its points.
#pragma once

#include "../common/quickclick.h"

#include <QDialog>

class GlobalInput;
class QCheckBox;
class QComboBox;
class KeyButton;
class NumberEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class QuickDialog : public QDialog {
public:
    // exec() result of "Pick points...": the caller runs the picker and opens
    // the dialog again (a Qt dialog that hides itself ends its exec()).
    static const int Pick = 2;

    // An empty `fixedName` makes a new set (`typedName` fills the name box);
    // otherwise the name is fixed.
    QuickDialog(const QString &fixedName, const QString &typedName, const QuickSet &set, GlobalInput *input,
                QWidget *parent = nullptr);
    QString name() const;
    // The set with the hotkey and gap from the boxes.
    QuickSet set() const;

private:
    void fillTable();
    void move(int delta);
    void removePoint();
    void check();
    void updateGame();
    int fps() const;

    QuickSet set_;
    QLineEdit *name_;
    KeyButton *hotkey_;
    NumberEdit *gap_;
    QCheckBox *game_;
    QComboBox *fps_, *frames_;
    QLabel *gameInfo_;
    QTableWidget *points_;
    QPushButton *up_, *down_, *remove_, *pick_, *ok_;
    QLabel *problem_;
};
