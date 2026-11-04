#include "quickdialog.h"

#include "../common/daemon.h"
#include "../common/macro.h"

#include "globalinput.h"
#include "keybutton.h"
#include "numberedit.h"

#include <QCheckBox>
#include <QComboBox>
#include <QMessageBox>
#include <QIntValidator>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
const ClickType kTypes[] = {ClickType::Left, ClickType::Right, ClickType::Middle, ClickType::Double};
const char *const kTypeLabels[] = {"Left", "Right", "Middle", "Double"};
} // namespace

QuickDialog::QuickDialog(const QString &fixedName, const QString &typedName, const QuickSet &set, GlobalInput *input,
                         QWidget *parent)
    : QDialog(parent), set_(set), input_(input), fixedName_(fixedName) {
    const QString &name = fixedName;
    setWindowTitle(name.isEmpty() ? "New quick click" : "Edit '" + name + "'");
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    name_ = new QLineEdit(name.isEmpty() ? typedName : name, this);
    name_->setValidator(new QRegularExpressionValidator(QRegularExpression("[A-Za-z0-9_-]{0,40}"), name_));
    name_->setPlaceholderText("e.g. Farm");
    name_->setReadOnly(!name.isEmpty());
    hotkey_ = new KeyButton(set.hotkey, this);
    hotkey_->setEmptyText("Click, then press a key");
    hotkey_->setAcceptsMouse(true);
    gap_ = new NumberEdit(kMinGapUs / 1000.0, kMaxGapUs / 1000.0, 1, this);
    gap_->setValue(set.gapUs / 1000.0);
    form->addRow("Name", name_);
    form->addRow("Hotkey", hotkey_);
    form->addRow("Gap between points (ms)", gap_);
    layout->addLayout(form);
    auto *gapHelp = new QLabel("0.1 ms is the fastest that never drops a click. Raise it if an app misses some.", this);
    gapHelp->setObjectName("dim");
    gapHelp->setWordWrap(true);
    layout->addWidget(gapHelp);

    // Games read clicks and redraw menus once a frame: clicks faster than
    // that land on a menu that has not changed yet.
    game_ = new QCheckBox("For a game: wait for the game between points", this);
    auto *gameRow = new QHBoxLayout;
    fps_ = new QComboBox(this);
    fps_->setEditable(true);
    fps_->setValidator(new QIntValidator(1, 1000, fps_));
    for (const char *f : {"60", "120", "144", "165", "240"}) fps_->addItem(f);
    frames_ = new QComboBox(this);
    for (int f = 1; f <= 10; ++f) frames_->addItem(QString::number(f));
    gameRow->addWidget(new QLabel("Game frame rate (FPS)", this));
    gameRow->addWidget(fps_);
    gameRow->addSpacing(12);
    gameRow->addWidget(new QLabel("Frames to wait", this));
    gameRow->addWidget(frames_);
    gameRow->addStretch();
    gameInfo_ = new QLabel(this);
    gameInfo_->setObjectName("dim");
    gameInfo_->setWordWrap(true);
    layout->addWidget(game_);
    layout->addLayout(gameRow);
    layout->addWidget(gameInfo_);
    findGap_ = new QPushButton("Find the fastest gap...", this);
    findGap_->setToolTip("Try the points in the real app at gaps from 5 to 100 ms, and keep the fastest that works");
    auto *findRow = new QHBoxLayout;
    findRow->addWidget(findGap_);
    findRow->addStretch();
    layout->addLayout(findRow);
    connect(findGap_, &QPushButton::clicked, this, [this] { findGap(); });
    game_->setChecked(set.gameFps > 0);
    fps_->setCurrentText(QString::number(set.gameFps > 0 ? set.gameFps : 60));
    frames_->setCurrentText(QString::number(std::clamp(set.gameFrames, 1, 10)));
    connect(game_, &QCheckBox::toggled, this, [this] { updateGame(); });
    connect(fps_, &QComboBox::currentTextChanged, this, [this] { updateGame(); });
    connect(frames_, &QComboBox::currentTextChanged, this, [this] { updateGame(); });

    layout->addWidget(new QLabel("Points, clicked in this order", this));
    points_ = new QTableWidget(0, 4, this);
    points_->setHorizontalHeaderLabels({"#", "X", "Y", "Click"});
    points_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    points_->verticalHeader()->hide();
    points_->setSelectionBehavior(QAbstractItemView::SelectRows);
    points_->setSelectionMode(QAbstractItemView::SingleSelection);
    points_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    points_->setMinimumHeight(180);
    layout->addWidget(points_, 1);
    auto *row = new QHBoxLayout;
    pick_ = new QPushButton(set.points.empty() ? "Pick points..." : "Pick again...", this);
    pick_->setObjectName("accent");
    up_ = new QPushButton("Up", this);
    down_ = new QPushButton("Down", this);
    remove_ = new QPushButton("Remove", this);
    row->addWidget(pick_);
    row->addStretch();
    row->addWidget(up_);
    row->addWidget(down_);
    row->addWidget(remove_);
    layout->addLayout(row);

    problem_ = new QLabel(this);
    problem_->setObjectName("warn");
    problem_->setWordWrap(true);
    layout->addWidget(problem_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Save);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(name_, &QLineEdit::textChanged, this, [this] { check(); });
    connect(hotkey_, &KeyButton::changed, this, [this] { check(); });
    connect(pick_, &QPushButton::clicked, this, [this] { done(Pick); });
    connect(up_, &QPushButton::clicked, this, [this] { move(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { move(1); });
    connect(remove_, &QPushButton::clicked, this, [this] { removePoint(); });
    connect(points_, &QTableWidget::itemSelectionChanged, this, [this] { check(); });
    if (input) connect(input, &GlobalInput::input, this, [this](InputEvent ev) { hotkey_->offerGlobal(ev); });
    fillTable();
    updateGame();
    check();
    resize(520, 640);
}

QString QuickDialog::name() const { return name_->text(); }

QuickSet QuickDialog::set() const {
    QuickSet s = set_;
    s.hotkey = hotkey_->code();
    if (game_->isChecked() && fps() > 0) {
        s.gameFps = fps();
        s.gameFrames = frames_->currentText().toInt();
        s.gapUs = gameGapUs(s.gameFps, s.gameFrames);
    } else {
        s.gameFps = 0;
        s.gapUs = std::clamp(int(std::lround(gap_->value() * 1000)), kMinGapUs, kMaxGapUs);
    }
    return s;
}

void QuickDialog::findGap() {
    if (!input_ || !input_->available()) {
        QMessageBox::warning(this, "Find the fastest gap",
                             "This needs the keyboard to be readable (the input group, see the box at the top).");
        return;
    }
    // Enter keeps a gap and Esc stops, so neither can be the hotkey here.
    int key = hotkey_->code();
    if (key == 28 || key == 96 || key == 1) {
        QMessageBox::warning(this, "Find the fastest gap",
                             "Enter and Esc answer the finder, so pick another hotkey for the try runs.");
        return;
    }
    done(FindGap);
}

void QuickDialog::showNote(const QString &note) { problem_->setText(note); }

int QuickDialog::fps() const { return std::clamp(fps_->currentText().toInt(), 0, 1000); }

void QuickDialog::updateGame() {
    bool on = game_->isChecked();
    fps_->setEnabled(on);
    frames_->setEnabled(on);
    gap_->setEnabled(!on);
    if (on && fps() > 0) {
        int us = gameGapUs(fps(), frames_->currentText().toInt());
        gap_->setValue(us / 1000.0);
        gameInfo_->setText(QString("%1 ms between points (%2 frame%3 at %4 FPS). Use the game's FPS, not the "
                                   "monitor's refresh rate. 2 frames: one to take the click, one to redraw the menu.")
                               .arg(us / 1000.0, 0, 'f', 1)
                               .arg(frames_->currentText())
                               .arg(frames_->currentText() == "1" ? "" : "s")
                               .arg(fps()));
    } else {
        gameInfo_->setText(on ? "Type the game's frame rate." : "");
    }
}

void QuickDialog::fillTable() {
    int keep = points_->currentRow();
    points_->setRowCount(0);
    for (size_t i = 0; i < set_.points.size(); ++i) {
        const QuickPoint &p = set_.points[i];
        int r = points_->rowCount();
        points_->insertRow(r);
        points_->setItem(r, 0, new QTableWidgetItem(QString::number(i + 1)));
        points_->setItem(r, 1, new QTableWidgetItem(QString::number(p.x)));
        points_->setItem(r, 2, new QTableWidgetItem(QString::number(p.y)));
        auto *type = new QComboBox(points_);
        for (const char *label : kTypeLabels) type->addItem(label);
        type->setCurrentIndex(int(std::find(std::begin(kTypes), std::end(kTypes), p.type) - std::begin(kTypes)));
        connect(type, &QComboBox::currentIndexChanged, this, [this, i](int index) {
            if (i < set_.points.size() && index >= 0) set_.points[i].type = kTypes[index];
        });
        points_->setCellWidget(r, 3, type);
    }
    if (keep >= 0 && keep < points_->rowCount()) points_->selectRow(keep);
}

void QuickDialog::move(int delta) {
    int r = points_->currentRow(), to = r + delta;
    if (r < 0 || to < 0 || to >= int(set_.points.size())) return;
    std::swap(set_.points[size_t(r)], set_.points[size_t(to)]);
    fillTable();
    points_->selectRow(to);
}

void QuickDialog::removePoint() {
    int r = points_->currentRow();
    if (r < 0 || r >= int(set_.points.size())) return;
    set_.points.erase(set_.points.begin() + r);
    fillTable();
    check();
}

void QuickDialog::check() {
    bool selected = points_->currentRow() >= 0 && !points_->selectedItems().isEmpty();
    up_->setEnabled(selected && points_->currentRow() > 0);
    down_->setEnabled(selected && points_->currentRow() + 1 < points_->rowCount());
    remove_->setEnabled(selected);
    QString problem;
    if (!validName(name_->text().toStdString()))
        problem = "Give it a name (letters, digits, _ and -).";
    else if (hotkey_->code() < 0)
        problem = "Choose the hotkey: click the button, then press a key.";
    else if (set_.points.empty())
        problem = "Pick the points: click Pick points..., then click where it should click.";
    problem_->setText(problem);
    ok_->setEnabled(problem.isEmpty());
    findGap_->setEnabled(!set_.points.empty() && hotkey_->code() >= 0);
}
