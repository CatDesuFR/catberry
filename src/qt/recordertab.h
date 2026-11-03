// The Recorder tab: record keys and clicks, play them back, and the list
// of saved macros.
#pragma once

#include "../common/keys.h"
#include "../common/macro.h"
#include "../common/output.h"
#include "../common/player.h"

#include <QTimer>
#include <QWidget>

#include <memory>
#include <set>

class GlobalInput;
class KeyButton;
class QLabel;
class QListWidget;
class QPushButton;
class NumberEdit;

class RecorderTab : public QWidget {
    Q_OBJECT
public:
    // `output` may be a do-nothing output when /dev/uinput is not usable.
    RecorderTab(Output &output, GlobalInput *input, QWidget *parent = nullptr);
    ~RecorderTab() override;

    void globalInput(const InputEvent &ev);
    // A key pressed in this window (evdev code), used when there is no global input.
    void localKey(int code);
    bool busy() const; // recording, playing or counting down

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void toggleRecording();
    void togglePlayback(bool delayed);
    void startPlayback();
    void stopPlayback();
    void refresh();
    void refreshList();
    void loadSelected();
    void saveAs();
    void renameSelected();
    void deleteSelected();
    QString selectedName() const;
    void loadSettings();
    void saveSettings() const;

    Output &output_;
    GlobalInput *input_;
    Player player_; // after output_: stopped before it goes

    Macro macro_;
    QString currentName_;
    bool recording_ = false;
    Clock::time_point recordStart_{};
    std::set<int> pressed_;
    bool countingDown_ = false;
    Clock::time_point startAt_{};
    QString message_;

    QPushButton *record_, *play_, *saveAs_, *rename_, *delete_;
    NumberEdit *repeat_;
    KeyButton *recordKey_, *playKey_;
    QListWidget *list_;
    QLabel *status_;
    QTimer tick_;
};
