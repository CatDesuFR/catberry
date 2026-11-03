#include "quickdialog.h"

#include "../common/macro.h"
#include "../common/picker.h"
#include "globalinput.h"
#include "keybutton.h"
#include "numberedit.h"

#include <QComboBox>
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

QuickDialog::QuickDialog(const QString &name, const QuickSet &set, GlobalInput *input, QWidget *parent)
    : QDialog(parent), set_(set) {
    setWindowTitle(name.isEmpty() ? "New quick click" : "Edit '" + name + "'");
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    name_ = new QLineEdit(name, this);
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

    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        set_.hotkey = hotkey_->code();
        set_.gapUs = std::clamp(int(std::lround(gap_->value() * 1000)), kMinGapUs, kMaxGapUs);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(name_, &QLineEdit::textChanged, this, [this] { check(); });
    connect(hotkey_, &KeyButton::changed, this, [this] { check(); });
    connect(pick_, &QPushButton::clicked, this, [this] { pick(); });
    connect(up_, &QPushButton::clicked, this, [this] { move(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { move(1); });
    connect(remove_, &QPushButton::clicked, this, [this] { removePoint(); });
    connect(points_, &QTableWidget::itemSelectionChanged, this, [this] { check(); });
    if (input) connect(input, &GlobalInput::input, this, [this](InputEvent ev) { hotkey_->offerGlobal(ev); });
    fillTable();
    check();
    resize(480, 560);
}

QString QuickDialog::name() const { return name_->text(); }

void QuickDialog::pick() {
    // This dialog and the main window out of the way while points are picked.
    hide();
    if (parentWidget()) parentWidget()->window()->hide();
    QuickSet picked = set_;
    if (pickPoints(picked)) set_ = picked;
    if (parentWidget()) parentWidget()->window()->show();
    show();
    activateWindow();
    pick_->setText("Pick again...");
    fillTable();
    check();
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
}
