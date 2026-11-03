#include "mtoolswindow.h"

#include "globalinput.h"
#include "keybutton.h"
#include "problems.h"
#include "recordertab.h"
#include "theme.h"

#include <QApplication>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
// Stands in when /dev/uinput cannot be used, so the window still opens
// and can say why nothing happens.
class NoOutput : public Output {
public:
    const char *name() const override { return "none"; }
    void key(int, bool) override {}
};
} // namespace

MtoolsWindow::MtoolsWindow() {
    setWindowTitle("Catberry Macro Recorder");
    std::string error;
    output_ = createOutput("", true, error);
    bool outputMissing = !output_;
    if (!output_) output_.reset(new NoOutput);
    input_ = new GlobalInput(this);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    problems_ = new ProblemsBanner(central);
    problems_->setInputMissing(!input_->available(), "Recording and the hotkeys are off: this user cannot read the "
                                                     "keyboard and mouse");
    problems_->setOutputMissing(outputMissing);
    layout->addWidget(problems_);

    auto *tabs = new QTabWidget(central);
    recorder_ = new RecorderTab(*output_, input_, tabs);
    tabs->addTab(recorder_, "Recorder");
    tabs->addTab(new QWidget, "Hotkey macros");
    layout->addWidget(tabs, 1);
    setCentralWidget(central);

    connect(input_, &GlobalInput::input, this, [this](InputEvent ev) { recorder_->globalInput(ev); });
    resize(560, 620);
}

MtoolsWindow::~MtoolsWindow() = default;

void MtoolsWindow::keyPressEvent(QKeyEvent *event) {
    if (!event->isAutoRepeat()) {
        int code = evdevCode(event);
        if (code >= 0) recorder_->localKey(code);
    }
    QMainWindow::keyPressEvent(event);
}

void MtoolsWindow::closeEvent(QCloseEvent *event) {
    // The tabs stop playback and save their settings as they go.
    QMainWindow::closeEvent(event);
}

int mtoolsGui(int quitAfterMs) {
    int argc = 1;
    char name[] = "catberry-mtools";
    char *argv[] = {name, nullptr};
    QApplication app(argc, argv);
    app.setApplicationName("catberry-mtools");
    app.setDesktopFileName("catberry-mtools");
    app.setStyleSheet(catberryStyleSheet());
    MtoolsWindow window;
    window.show();
    if (quitAfterMs > 0)
        QTimer::singleShot(quitAfterMs, &app, [&] {
            // Tests can ask for a picture of the window as it closes.
            QString shot = qEnvironmentVariable("CATBERRY_SCREENSHOT");
            if (!shot.isEmpty()) window.grab().save(shot);
            app.quit();
        });
    return app.exec();
}
