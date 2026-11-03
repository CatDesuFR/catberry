// The macro recorder's window: a Recorder tab and a Hotkey macros tab,
// under the box that explains missing permissions.
#pragma once

#include "../common/output.h"

#include <QMainWindow>

#include <memory>

class GlobalInput;
class HotkeysTab;
class ProblemsBanner;
class RecorderTab;

class MtoolsWindow : public QMainWindow {
    Q_OBJECT
public:
    MtoolsWindow();
    ~MtoolsWindow() override;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    std::unique_ptr<Output> output_; // before the tabs, which use it
    GlobalInput *input_;
    ProblemsBanner *problems_;
    RecorderTab *recorder_;
    HotkeysTab *hotkeys_ = nullptr;
};

// Opens the window and runs until it is closed. quitAfterMs > 0 closes it
// after that long (for tests).
int mtoolsGui(int quitAfterMs);
