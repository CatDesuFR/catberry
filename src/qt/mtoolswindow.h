// The macro recorder's window: a Recorder tab and a Hotkey macros tab.
#pragma once

#include <QMainWindow>

class MtoolsWindow : public QMainWindow {
    Q_OBJECT
public:
    MtoolsWindow();
    ~MtoolsWindow() override;
};

// Opens the window and runs until it is closed. quitAfterMs > 0 closes it
// after that long (for tests).
int mtoolsGui(int quitAfterMs);
