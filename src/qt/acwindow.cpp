#include "acwindow.h"

#include "../common/platform.h"
#include "globalinput.h"
#include "keybutton.h"
#include "problems.h"
#include "theme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QDoubleValidator>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace {

const int kStartDelaySec = 3;
const int kTargetCount = 5;
const char *const kTargetNames[kTargetCount] = {"Left", "Middle", "Right", "Scroll up", "Scroll down"};
const int kTargetCodes[kTargetCount] = {keycode::MouseLeft, keycode::MouseMiddle, keycode::MouseRight,
                                        keycode::WheelUp, keycode::WheelDown};

class NoOutput : public Output {
public:
    const char *name() const override { return "none"; }
    void key(int, bool) override {}
};

void setKind(QPushButton *b, const char *kind) {
    b->setObjectName(kind);
    b->style()->unpolish(b);
    b->style()->polish(b);
}

QString formatCps(double cps) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", cps);
    return buf;
}

} // namespace

// --- ClickTest -------------------------------------------------------------

ClickTest::ClickTest(QWidget *parent) : QFrame(parent) {
    setObjectName("testArea");
    setMinimumHeight(56);
}

void ClickTest::reset() { count_ = 0; }

void ClickTest::mousePressEvent(QMouseEvent *event) {
    if (!active_) return QFrame::mousePressEvent(event);
    // Our own clicks arriving back through the whole input stack.
    if (count_++ == 0) first_ = Clock::now();
    last_ = Clock::now();
    update();
}

QString ClickTest::text(bool keyTarget) const {
    if (keyTarget) return "Click test: counts mouse clicks only";
    if (count_ == 0) return "Click test: start, then hover here";
    double seconds = std::chrono::duration<double>(last_ - first_).count();
    if (count_ < 2 || seconds <= 0) return QString("Click test: %1 received").arg(count_);
    return QString("Click test: %1 CPS received (%2)").arg((count_ - 1) / seconds, 0, 'f', 1).arg(count_);
}

void ClickTest::paintEvent(QPaintEvent *event) {
    QFrame::paintEvent(event);
    QPainter p(this);
    p.setPen(theme::kDim);
    p.drawText(rect(), Qt::AlignCenter, text(keyTarget_));
}

// --- AcWindow --------------------------------------------------------------

AcWindow::AcWindow(const std::string &backend) {
    setWindowTitle("Catberry Autoclicker");
    std::string error;
    output_ = createOutput(backend, true, error);
    bool outputMissing = !output_;
    if (!output_) output_.reset(new NoOutput);
    clicker_.reset(new Clicker(*output_));
    input_ = new GlobalInput(this);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    problems_ = new ProblemsBanner(central);
    problems_->setInputMissing(!input_->available(), "The hotkey only works while this window is focused: this "
                                                     "user cannot read the keyboard");
    problems_->setOutputMissing(outputMissing);
    layout->addWidget(problems_);

    // Clicks per second, with - and +.
    auto *cpsRow = new QHBoxLayout;
    cpsRow->addWidget(new QLabel("Clicks per second", central), 1);
    auto *minus = new QPushButton("-", central);
    auto *plus = new QPushButton("+", central);
    // A plain text box: typed values are kept as typed (0.25 stays 0.25).
    cps_ = new QLineEdit(central);
    cps_->setAlignment(Qt::AlignCenter);
    cps_->setMinimumWidth(90);
    cps_->setMaximumWidth(110);
    for (QPushButton *b : {minus, plus}) b->setFixedWidth(40);
    cpsRow->addWidget(minus);
    cpsRow->addWidget(cps_);
    cpsRow->addWidget(plus);
    layout->addLayout(cpsRow);

    // What to click.
    layout->addWidget(new QLabel("Click with", central));
    auto *grid = new QGridLayout;
    targets_ = new QButtonGroup(this);
    targets_->setExclusive(true);
    for (int i = 0; i < kTargetCount; ++i) {
        auto *b = new QPushButton(kTargetNames[i], central);
        b->setCheckable(true);
        targets_->addButton(b, i);
        grid->addWidget(b, i / 3, i % 3);
    }
    keyButton_ = new KeyButton(-1, central);
    keyButton_->setCheckable(true);
    keyButton_->setPrefix("Key: ");
    keyButton_->setEmptyText("Key...");
    targets_->addButton(keyButton_, kTargetCount);
    grid->addWidget(keyButton_, 1, 2);
    layout->addLayout(grid);

    auto *hotkeyRow = new QHBoxLayout;
    hotkeyRow->addWidget(new QLabel("Start/stop hotkey", central), 1);
    hotkey_ = new KeyButton(keycode::F6, central);
    hotkey_->setMinimumWidth(150);
    hotkeyRow->addWidget(hotkey_);
    layout->addLayout(hotkeyRow);

    toggle_ = new QPushButton("Start", central);
    toggle_->setMinimumHeight(48);
    layout->addWidget(toggle_);
    test_ = new ClickTest(central);
    layout->addWidget(test_);
    status_ = new QLabel(central);
    info_ = new QLabel(central);
    info_->setObjectName("dim");
    layout->addWidget(status_);
    layout->addWidget(info_);
    layout->addStretch();
    setCentralWidget(central);

    connect(minus, &QPushButton::clicked, this, [this] { setCps(clicker_->cps() - 1); });
    connect(plus, &QPushButton::clicked, this, [this] { setCps(clicker_->cps() + 1); });
    connect(cps_, &QLineEdit::textEdited, this, [this](const QString &text) {
        // Applied while typing, when it is a number; shown cleaned up on Enter or leaving.
        bool ok = false;
        double v = QString(text).replace(',', '.').toDouble(&ok);
        if (ok && v > 0) clicker_->setCps(v);
    });
    connect(cps_, &QLineEdit::editingFinished, this, [this] { setCps(clicker_->cps()); });
    connect(targets_, &QButtonGroup::idClicked, this, [this](int id) {
        // "Key..." only becomes the target once a key was pressed (picked
        // below); until then the current target stays ticked.
        chooseTarget(id < kTargetCount ? kTargetCodes[id] : clicker_->code());
    });
    connect(keyButton_, &KeyButton::picked, this, [this](int code) {
        keyTarget_ = code;
        chooseTarget(code);
    });
    connect(hotkey_, &KeyButton::picked, this, [this](int code) { keyButton_->setForbidden(code); refresh(); });
    connect(toggle_, &QPushButton::clicked, this, [this] { startCountdown(); });
    connect(input_, &GlobalInput::input, this, [this](InputEvent ev) { globalInput(ev); });
    connect(&tick_, &QTimer::timeout, this, [this] {
        if (countingDown_ && Clock::now() >= startAt_) {
            countingDown_ = false;
            start();
        }
        refresh();
    });
    tick_.start(100);

    loadSettings();
    setCps(clicker_->cps());
    keyButton_->setCode(keyTarget_);
    keyButton_->setForbidden(hotkey_->code());
    hotkey_->setForbidden(keyTarget_);
    chooseTarget(clicker_->code());
    // While clicking, a click anywhere on the window stops it (our own
    // clicks land here too when the pointer is over it).
    qApp->installEventFilter(this);
    resize(420, sizeHint().height());
}

AcWindow::~AcWindow() {
    qApp->removeEventFilter(this);
    clicker_->setRunning(false);
    saveSettings();
}

void AcWindow::setCps(double cps) {
    clicker_->setCps(cps); // clamps to 0.1 - 1000
    cps_->setText(formatCps(clicker_->cps()));
}

void AcWindow::chooseTarget(int code) {
    clicker_->setCode(code);
    bool key = keyTarget_ >= 0 && code == keyTarget_;
    for (int i = 0; i < kTargetCount; ++i)
        if (kTargetCodes[i] == code) targets_->button(i)->setChecked(true);
    if (key) keyButton_->setChecked(true);
    hotkey_->setForbidden(keyTarget_);
    refresh();
}

bool AcWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() != QEvent::MouseButtonPress) return QMainWindow::eventFilter(watched, event);
    auto *w = qobject_cast<QWidget *>(watched);
    if (!w || w->window() != this) return QMainWindow::eventFilter(watched, event);
    if (clicker_->running() && w == test_) return false; // counted by the click test
    if (clicker_->running() || countingDown_) {
        stop();
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void AcWindow::keyPressEvent(QKeyEvent *event) {
    // Without global input, the hotkey still works while this window is focused.
    if (!input_->available() && !event->isAutoRepeat() && !hotkey_->capturing() && !keyButton_->capturing() &&
        evdevCode(event) == hotkey_->code())
        toggleNow();
    QMainWindow::keyPressEvent(event);
}

void AcWindow::globalInput(const InputEvent &ev) {
    if (hotkey_->capturing() || keyButton_->capturing()) {
        hotkey_->offerGlobal(ev);
        keyButton_->offerGlobal(ev);
        return;
    }
    if (KeyButton::anyBusy()) return;
    if (ev.down && ev.code == hotkey_->code()) toggleNow();
}

void AcWindow::startCountdown() {
    if (clicker_->running() || countingDown_) return stop();
    // Delayed, so the pointer can be moved off this window first.
    countingDown_ = true;
    startAt_ = Clock::now() + std::chrono::seconds(kStartDelaySec);
    refresh();
}

void AcWindow::start() {
    test_->reset();
    clicker_->setRunning(true);
    refresh();
}

void AcWindow::stop() {
    countingDown_ = false;
    clicker_->setRunning(false);
    refresh();
}

void AcWindow::toggleNow() {
    if (clicker_->running() || countingDown_)
        stop();
    else
        start();
}

void AcWindow::refresh() {
    bool running = clicker_->running();
    bool keyTarget = keyTarget_ >= 0 && clicker_->code() == keyTarget_;
    QString hk = QString::fromStdString(keyName(hotkey_->code()));
    if (running) {
        toggle_->setText("Stop");
        setKind(toggle_, "stop");
        status_->setText("Running - " + hk + " or click this window to stop");
    } else if (countingDown_) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(startAt_ - Clock::now()).count();
        toggle_->setText(QString("Starting in %1... (click to cancel)").arg(std::max(0LL, (long long)left) / 1000 + 1));
        setKind(toggle_, "accent");
        status_->setText("Move the pointer to where it should click");
    } else {
        toggle_->setText("Start");
        setKind(toggle_, "go");
        status_->setText("Stopped");
    }
    test_->setActive(running);
    test_->keyTarget_ = keyTarget;
    test_->update();
    bool idle = !running && !countingDown_;
    for (QAbstractButton *b : targets_->buttons()) b->setEnabled(idle);
    hotkey_->setEnabled(idle);
    cps_->setEnabled(idle);
    info_->setText(QString("Output: %1 | hotkey: %2")
                       .arg(output_->name())
                       .arg(input_->available() ? "global" : "only while this window is focused"));
}

void AcWindow::loadSettings() {
    bool keyIsTarget = false;
    int hotkey = keycode::F6;
    std::ifstream in(configDir() / "ac.conf");
    // Location used by the standalone catberryacgui (ac 1.0.1 and older).
    if (!in) in.open(configDir().parent_path() / "catberryacgui.conf");
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        double number = std::strtod(value.c_str(), nullptr);
        if (key == "cps" && number > 0) {
            clicker_->setCps(number);
        } else if (key == "target" && value != "key" && number >= 0 && number < kTargetCount) {
            clicker_->setCode(kTargetCodes[static_cast<int>(number)]);
        } else if (key == "target" && value == "key") {
            keyIsTarget = true;
        } else if (key == "key" && parseKey(value) > 0 && parseKey(value) < 256) {
            keyTarget_ = parseKey(value);
        } else if (key == "toggle_key" && parseKey(value) >= 0) {
            hotkey = parseKey(value);
        } else if (key == "hotkey" && number >= 9 && number <= 255) {
            // Older Linux builds stored an X keycode: the key code plus 8.
            hotkey = static_cast<int>(number) - 8;
        }
    }
    hotkey_->setCode(hotkey);
    if (keyIsTarget && keyTarget_ >= 0) clicker_->setCode(keyTarget_);
}

void AcWindow::saveSettings() const {
    std::string target = "0";
    for (int i = 0; i < kTargetCount; ++i)
        if (kTargetCodes[i] == clicker_->code()) target = std::to_string(i);
    if (keyTarget_ >= 0 && clicker_->code() == keyTarget_) target = "key";
    std::ofstream out(configDir() / "ac.conf");
    out << "cps=" << formatCps(clicker_->cps()).toStdString() << "\n"
        << "target=" << target << "\n"
        << (keyTarget_ >= 0 ? "key=" + keyName(keyTarget_) + "\n" : std::string())
        << "toggle_key=" << keyName(hotkey_->code()) << "\n";
}

int acGui(int argc, char **argv) {
    std::string backend;
    int quitAfterMs = 0;
    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--backend=", 0) == 0) {
            backend = arg.substr(10);
        } else if (arg == "--quit-after" && i + 1 < argc) {
            quitAfterMs = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "catberry ac gui: unknown option '%s'\n", arg.c_str());
            return 2;
        }
    }
    int qargc = 1;
    char name[] = "catberry-ac";
    char *qargv[] = {name, nullptr};
    QApplication app(qargc, qargv);
    app.setApplicationName("catberry-ac");
    app.setDesktopFileName("catberry-ac");
    app.setStyleSheet(catberryStyleSheet());
    AcWindow window(backend);
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
