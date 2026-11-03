// The Hotkey macros tab: the configs of catberry -config, with their
// status, and buttons to create, edit, delete, load and unload them.
#pragma once

#include <QTimer>
#include <QWidget>

class GlobalInput;
class QPushButton;
class QTableWidget;

class HotkeysTab : public QWidget {
public:
    HotkeysTab(GlobalInput *input, QWidget *parent = nullptr);

private:
    void reload();          // rows from the configs folder
    void refreshStatus();   // Running / Stopped, every second
    QString selectedName() const;
    void edit(const QString &name); // empty: a new one
    void remove();
    bool load(const QString &name);
    void unload(const QString &name);
    void unloadAll();
    void updateButtons();

    GlobalInput *input_;
    QTableWidget *table_;
    QPushButton *new_, *edit_, *delete_, *load_, *unload_, *unloadAll_;
    QTimer refresh_;
};
