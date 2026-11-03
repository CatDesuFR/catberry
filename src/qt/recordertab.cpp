#include "recordertab.h"

#include "../common/platform.h"
#include "globalinput.h"
#include "keybutton.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace {
const int kStartDelaySec = 3;

void setKind(QPushButton *b, const char *kind) {
    b->setObjectName(kind);
    b->style()->unpolish(b);
    b->style()->polish(b);
}

QString describe(const Macro &m) {
    return QString("%1 events, %2 s").arg(m.events.size()).arg(m.durationUs / 1e6, 0, 'f', 1);
}
} // namespace

RecorderTab::RecorderTab(Output &output, GlobalInput *input, QWidget *parent)
    : QWidget(parent), output_(output), input_(input),
      player_(output, [this] { QMetaObject::invokeMethod(this, [this] { refresh(); }, Qt::QueuedConnection); }) {
    auto *layout = new QVBoxLayout(this);

    auto *buttons = new QHBoxLayout;
    record_ = new QPushButton("Record", this);
    play_ = new QPushButton("Play", this);
    for (QPushButton *b : {record_, play_}) b->setMinimumHeight(44);
    buttons->addWidget(record_);
    buttons->addWidget(play_);
    layout->addLayout(buttons);

    auto *form = new QFormLayout;
    repeat_ = new QSpinBox(this);
    repeat_->setRange(0, 100000);
    repeat_->setValue(1);
    repeat_->setSpecialValueText("until stopped");
    // The theme cannot draw the arrows; typing and the mouse wheel change it.
    repeat_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    recordKey_ = new KeyButton(keycode::F8, this);
    playKey_ = new KeyButton(keycode::F9, this);
    form->addRow("Repeat (0 = until stopped)", repeat_);
    form->addRow("Record hotkey", recordKey_);
    form->addRow("Play hotkey", playKey_);
    layout->addLayout(form);

    layout->addWidget(new QLabel("Saved macros (click one to load it)", this));
    list_ = new QListWidget(this);
    layout->addWidget(list_, 1);
    auto *listButtons = new QHBoxLayout;
    saveAs_ = new QPushButton("Save as...", this);
    rename_ = new QPushButton("Rename...", this);
    delete_ = new QPushButton("Delete", this);
    listButtons->addWidget(saveAs_);
    listButtons->addWidget(rename_);
    listButtons->addWidget(delete_);
    listButtons->addStretch();
    layout->addLayout(listButtons);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    layout->addWidget(status_);

    connect(record_, &QPushButton::clicked, this, [this] { toggleRecording(); });
    connect(play_, &QPushButton::clicked, this, [this] { togglePlayback(true); });
    connect(recordKey_, &KeyButton::changed, this, [this](int code) {
        playKey_->setForbidden(code);
        refresh();
    });
    connect(playKey_, &KeyButton::changed, this, [this](int code) {
        recordKey_->setForbidden(code);
        refresh();
    });
    connect(list_, &QListWidget::itemClicked, this, [this] { loadSelected(); });
    connect(list_, &QListWidget::itemSelectionChanged, this, [this] { refresh(); });
    connect(saveAs_, &QPushButton::clicked, this, [this] { saveAs(); });
    connect(rename_, &QPushButton::clicked, this, [this] { renameSelected(); });
    connect(delete_, &QPushButton::clicked, this, [this] { deleteSelected(); });
    connect(&tick_, &QTimer::timeout, this, [this] {
        if (countingDown_ && Clock::now() >= startAt_) {
            countingDown_ = false;
            startPlayback();
        }
        refresh();
    });
    tick_.start(100);

    loadSettings();
    recordKey_->setForbidden(playKey_->code());
    playKey_->setForbidden(recordKey_->code());
    refreshList();
    refresh();
    // While playing, a click anywhere on the window stops it (our own
    // clicks may land here too).
    qApp->installEventFilter(this);
}

RecorderTab::~RecorderTab() {
    qApp->removeEventFilter(this);
    player_.stop();
    saveSettings();
}

bool RecorderTab::busy() const { return recording_ || player_.playing() || countingDown_; }

bool RecorderTab::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::MouseButtonPress && (player_.playing() || countingDown_)) {
        auto *w = qobject_cast<QWidget *>(watched);
        if (w && w->window() == window()) {
            stopPlayback();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void RecorderTab::globalInput(const InputEvent &ev) {
    if (recordKey_->capturing() || playKey_->capturing()) {
        recordKey_->offerGlobal(ev);
        playKey_->offerGlobal(ev);
        return;
    }
    if (recordKey_->recentlyCaptured() || playKey_->recentlyCaptured()) return;
    if (ev.code == recordKey_->code() || ev.code == playKey_->code()) {
        if (!ev.down) return;
        if (ev.code == recordKey_->code())
            toggleRecording();
        else
            togglePlayback(false);
        return;
    }
    if (!recording_) return;
    if (!isWheel(ev.code)) {
        // Only releases of keys pressed during the recording.
        if (!ev.down && !pressed_.erase(ev.code)) return;
        if (ev.down) pressed_.insert(ev.code);
    }
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - recordStart_).count();
    macro_.events.push_back({us, ev.code, ev.down});
}

void RecorderTab::localKey(int code) {
    if (input_->available() || recordKey_->capturing() || playKey_->capturing()) return;
    if (code == playKey_->code()) togglePlayback(false);
}

void RecorderTab::toggleRecording() {
    if (player_.playing() || countingDown_) return;
    message_.clear();
    if (!recording_) {
        if (!input_->available()) {
            message_ = "Recording needs the keyboard and mouse to be readable (see the box above).";
            refresh();
            return;
        }
        macro_ = Macro();
        currentName_.clear();
        pressed_.clear();
        recordStart_ = Clock::now();
        recording_ = true;
        refresh();
        return;
    }
    recording_ = false;
    macro_.durationUs = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - recordStart_).count();
    // Keys still held at the end: drop their press, so playback leaves
    // nothing stuck down.
    for (int code : pressed_) {
        for (auto it = macro_.events.rbegin(); it != macro_.events.rend(); ++it) {
            if (it->code == code && it->down) {
                macro_.events.erase(std::next(it).base());
                break;
            }
        }
    }
    refresh();
}

void RecorderTab::togglePlayback(bool delayed) {
    if (recording_) return;
    message_.clear();
    if (player_.playing() || countingDown_) {
        stopPlayback();
    } else if (macro_.events.empty()) {
        message_ = "Nothing to play - record or load a macro first.";
    } else if (delayed) {
        countingDown_ = true;
        startAt_ = Clock::now() + std::chrono::seconds(kStartDelaySec);
    } else {
        startPlayback();
    }
    refresh();
}

void RecorderTab::startPlayback() {
    player_.start(macro_, repeat_->value());
    refresh();
}

void RecorderTab::stopPlayback() {
    countingDown_ = false;
    player_.stop();
    refresh();
}

void RecorderTab::refresh() {
    bool playing = player_.playing();
    record_->setText(recording_ ? "Stop recording" : "Record");
    setKind(record_, recording_ ? "stop" : "");
    record_->setEnabled(!playing && !countingDown_ && (recording_ || input_->available()));
    if (playing) {
        play_->setText("Stop");
        setKind(play_, "stop");
    } else if (countingDown_) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(startAt_ - Clock::now()).count();
        play_->setText(QString("Starting in %1... (click to cancel)").arg(std::max(0LL, (long long)left) / 1000 + 1));
        setKind(play_, "accent");
    } else {
        play_->setText("Play");
        setKind(play_, "go");
    }
    play_->setEnabled(!recording_);
    bool idle = !busy();
    bool selected = !selectedName().isEmpty();
    list_->setEnabled(idle);
    saveAs_->setEnabled(idle && !macro_.events.empty());
    rename_->setEnabled(idle && selected);
    delete_->setEnabled(idle && selected);
    repeat_->setEnabled(idle);
    recordKey_->setEnabled(idle);
    playKey_->setEnabled(idle);

    QString rec = QString::fromStdString(keyName(recordKey_->code()));
    QString pl = QString::fromStdString(keyName(playKey_->code()));
    QString status;
    if (!message_.isEmpty())
        status = message_;
    else if (recording_)
        status = QString("Recording... %1 events (%2 to stop)").arg(macro_.events.size()).arg(rec);
    else if (playing)
        status = QString("Playing - %1 or click this window to stop").arg(pl);
    else if (countingDown_)
        status = "Move the pointer to where it should click";
    else if (macro_.events.empty())
        status = "Nothing recorded yet";
    else
        status = QString("Ready%1: %2").arg(currentName_.isEmpty() ? "" : " ('" + currentName_ + "')").arg(describe(macro_));
    status_->setText(status);
}

void RecorderTab::refreshList() {
    QString keep = selectedName();
    list_->clear();
    for (const std::string &name : macroNames()) {
        Macro m;
        QString text = QString::fromStdString(name);
        if (loadMacro(name, m)) text += "  -  " + describe(m);
        auto *item = new QListWidgetItem(text, list_);
        item->setData(Qt::UserRole, QString::fromStdString(name));
        if (QString::fromStdString(name) == keep) list_->setCurrentItem(item);
    }
}

QString RecorderTab::selectedName() const {
    QListWidgetItem *item = list_->currentItem();
    return item && item->isSelected() ? item->data(Qt::UserRole).toString() : QString();
}

void RecorderTab::loadSelected() {
    QString name = selectedName();
    if (name.isEmpty() || busy()) return;
    Macro m;
    if (loadMacro(name.toStdString(), m)) {
        macro_ = m;
        currentName_ = name;
        message_ = "Loaded '" + name + "'";
    } else {
        message_ = "Could not read '" + name + "'";
    }
    refresh();
}

void RecorderTab::saveAs() {
    if (macro_.events.empty()) return;
    bool ok = false;
    QString name = QInputDialog::getText(this, "Save macro", "Name (letters, digits, _ and -):", QLineEdit::Normal,
                                         currentName_, &ok)
                       .trimmed();
    if (!ok || name.isEmpty()) return;
    if (!validName(name.toStdString())) {
        QMessageBox::warning(this, "Save macro", "Use only letters, digits, _ and - in the name.");
        return;
    }
    const auto names = macroNames();
    if (std::find(names.begin(), names.end(), name.toStdString()) != names.end() &&
        QMessageBox::question(this, "Save macro", "'" + name + "' already exists. Replace it?") != QMessageBox::Yes)
        return;
    if (saveMacro(name.toStdString(), macro_)) {
        currentName_ = name;
        message_ = "Saved '" + name + "'";
    } else {
        message_ = "Could not save '" + name + "'";
    }
    refreshList();
    refresh();
}

void RecorderTab::renameSelected() {
    QString from = selectedName();
    if (from.isEmpty()) return;
    bool ok = false;
    QString to =
        QInputDialog::getText(this, "Rename macro", "New name:", QLineEdit::Normal, from, &ok).trimmed();
    if (!ok || to.isEmpty() || to == from) return;
    if (!renameMacro(from.toStdString(), to.toStdString())) {
        QMessageBox::warning(this, "Rename macro",
                             validName(to.toStdString()) ? "'" + to + "' already exists."
                                                         : QString("Use only letters, digits, _ and - in the name."));
        return;
    }
    if (currentName_ == from) currentName_ = to;
    message_ = "Renamed '" + from + "' to '" + to + "'";
    refreshList();
    refresh();
}

void RecorderTab::deleteSelected() {
    QString name = selectedName();
    if (name.isEmpty()) return;
    if (QMessageBox::question(this, "Delete macro", "Delete '" + name + "'?") != QMessageBox::Yes) return;
    if (deleteMacro(name.toStdString())) {
        if (currentName_ == name) currentName_.clear();
        message_ = "Deleted '" + name + "'";
    }
    refreshList();
    refresh();
}

void RecorderTab::loadSettings() {
    std::ifstream in(configDir() / "mtools.conf");
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "repeat" && std::atoi(value.c_str()) >= 0)
            repeat_->setValue(std::atoi(value.c_str()));
        else if (key == "record_hotkey" && parseKey(value) > 0)
            recordKey_->setCode(parseKey(value));
        else if (key == "play_hotkey" && parseKey(value) > 0)
            playKey_->setCode(parseKey(value));
        else if (key == "name" && validName(value)) {
            Macro m;
            if (loadMacro(value, m)) {
                macro_ = m;
                currentName_ = QString::fromStdString(value);
            }
        }
    }
}

void RecorderTab::saveSettings() const {
    std::ofstream out(configDir() / "mtools.conf");
    out << "repeat=" << repeat_->value() << "\n"
        << "record_hotkey=" << keyName(recordKey_->code()) << "\n"
        << "play_hotkey=" << keyName(playKey_->code()) << "\n"
        << "name=" << currentName_.toStdString() << "\n";
}
