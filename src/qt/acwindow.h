// The autoclicker's window.
#pragma once

#include "../common/clicker.h"
#include "../common/keys.h"
#include "../common/output.h"

#include <QFrame>
#include <QMainWindow>
#include <QTimer>

#include <memory>

class GlobalInput;
class KeyButton;
class ProblemsBanner;
class QButtonGroup;
class QLineEdit;
class QLabel;
class QPushButton;

// Counts the clicks that arrive while the pointer is over it.
class ClickTest : public QFrame {
public:
    explicit ClickTest(QWidget *parent = nullptr);
    void reset();
    void setActive(bool on) { active_ = on; }
    QString text(bool keyTarget) const;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    bool active_ = false;
    int count_ = 0;
    Clock::time_point first_{}, last_{};
    bool keyTarget_ = false;
    friend class AcWindow;
};

class AcWindow : public QMainWindow {
    Q_OBJECT
public:
    AcWindow(const std::string &backend);
    ~AcWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void globalInput(const InputEvent &ev);
    void startCountdown();
    void start();
    void stop();
    void toggleNow();
    void refresh();
    void chooseTarget(int code);
    void setCps(double cps); // clamps, applies and shows it
    void loadSettings();
    void saveSettings() const;

    std::unique_ptr<Output> output_;   // before clicker_, which uses it
    std::unique_ptr<Clicker> clicker_; // destroyed first: stops clicking
    GlobalInput *input_;
    int keyTarget_ = -1; // keyboard key picked with "Key..."
    bool countingDown_ = false;
    Clock::time_point startAt_{};

    ProblemsBanner *problems_;
    QLineEdit *cps_;
    QButtonGroup *targets_;
    KeyButton *keyButton_, *hotkey_;
    QPushButton *toggle_;
    ClickTest *test_;
    QLabel *status_, *info_;
    QTimer tick_;
};

// Opens the window and runs until it is closed. Options: --backend=uinput,
// --quit-after MS (closes after MS milliseconds, for tests).
int acGui(int argc, char **argv);
