#include "gapfinder.h"

#include "../common/daemon.h"
#include "../common/output.h"
#include "../common/shakecursor.h"
#include "globalinput.h"

#include <QEventLoop>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

namespace {

// Gaps to try, fastest first (ms).
const int kGapsMs[] = {5, 10, 15, 20, 25, 30, 40, 50, 75, 100};
const int kGapCount = int(sizeof kGapsMs / sizeof kGapsMs[0]);
const int kEnter = 28, kKeypadEnter = 96; // evdev codes

// A try is not stopped by the keyboard: it is short, and the user's hotkey
// presses here mean "next try".
class NoInput : public KeyWatcher {
public:
    bool next(InputEvent &, int) override { return false; }
    bool stopRequested() const override { return false; }
};

} // namespace

int findFastestGap(const QuickSet &original, GlobalInput *input, QWidget *) {
    std::string error;
    std::unique_ptr<Output> output = createOutput("", false, error, true);
    if (!output || !output->canPoint()) return -1;

    // The strip: on top, and clicks go through it to the game.
    QWidget strip(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool |
                                Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    strip.setAttribute(Qt::WA_ShowWithoutActivating);
    strip.setObjectName("problems"); // the red-bordered panel look
    auto *layout = new QVBoxLayout(&strip);
    auto *text = new QLabel(&strip);
    text->setWordWrap(true);
    layout->addWidget(text);
    strip.setFixedWidth(620);

    QuickSet set = original;
    const QString key = QString::fromStdString(keyName(set.hotkey));
    int next = 0;   // index of the next gap to try
    int result = -1;
    QEventLoop loop;
    auto show = [&](const QString &message) {
        text->setText(message);
        strip.adjustSize();
        if (QScreen *s = QGuiApplication::primaryScreen())
            strip.move(s->geometry().center().x() - strip.width() / 2, s->geometry().top() + 12);
        strip.show();
        strip.raise();
    };
    show(QString("<b>Find the fastest gap</b> - try 1 of %1: <b>%2 ms</b><br>Go to the app or game and open the "
                 "menu, then press <b>%3</b> to click the points. <b>Esc</b>: stop.")
             .arg(kGapCount)
             .arg(kGapsMs[0])
             .arg(key));

    HotkeyState hotkey;
    QObject::connect(input, &GlobalInput::input, &loop, [&](InputEvent ev) {
        if (hotkey.pressed(ev, set.hotkey)) {
            if (next >= kGapCount) {
                show("None of the gaps worked; the set is unchanged. <b>Esc</b>: close.");
                return;
            }
            set.gapUs = kGapsMs[next] * 1000;
            {
                ShakeCursorPause shake;
                shake.pause();
                NoInput keys;
                HotkeyState none;
                runQuickSet(set, *output, keys, none);
            } // Shake Cursor comes back here
            ++next;
            QString more = next < kGapCount ? QString("If not, open the menu again and press <b>%1</b> to try "
                                                      "<b>%2 ms</b>.")
                                                  .arg(key)
                                                  .arg(kGapsMs[next])
                                            : QString("That was the slowest one.");
            show(QString("Tried <b>%1 ms</b>. Did every point come out right? <b>Enter</b>: yes, keep %1 ms. %2 "
                         "<b>Esc</b>: stop.")
                     .arg(kGapsMs[next - 1])
                     .arg(more));
            return;
        }
        if (!ev.down) return;
        if ((ev.code == kEnter || ev.code == kKeypadEnter) && next > 0) {
            result = kGapsMs[next - 1] * 1000;
            loop.quit();
        } else if (ev.code == keycode::Escape) {
            loop.quit();
        }
    });
    loop.exec();
    strip.hide();
    return result;
}
