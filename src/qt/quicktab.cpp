#include "quicktab.h"

#include "../common/daemon.h"
#include "../common/macro.h"
#include "../common/quickclick.h"
#include "../common/picker.h"
#include "quickdialog.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
enum Column { Name, Points, Hotkey, Gap, Status, ColumnCount };

std::vector<std::string> loadedQuickNames() {
    std::vector<std::string> names;
    const std::string prefix = quickPidName("");
    for (const std::string &pid : loadedNames())
        if (pid.compare(0, prefix.size(), prefix) == 0 && loadedPid(pid)) names.push_back(pid.substr(prefix.size()));
    return names;
}
} // namespace

QuickTab::QuickTab(GlobalInput *input, QWidget *parent) : QWidget(parent), input_(input) {
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel("A quick click clicks a set of screen points in order, as fast as possible, each time "
                             "you press its hotkey. Loaded ones keep working after this window is closed.",
                             this);
    intro->setObjectName("dim");
    intro->setWordWrap(true);
    layout->addWidget(intro);

    table_ = new QTableWidget(0, ColumnCount, this);
    table_->setHorizontalHeaderLabels({"Name", "Points", "Hotkey", "Gap", "Status"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(Name, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_, 1);

    auto *row1 = new QHBoxLayout, *row2 = new QHBoxLayout;
    new_ = new QPushButton("New...", this);
    edit_ = new QPushButton("Edit...", this);
    delete_ = new QPushButton("Delete", this);
    load_ = new QPushButton("Load", this);
    load_->setObjectName("go");
    unload_ = new QPushButton("Unload", this);
    unloadAll_ = new QPushButton("Unload all", this);
    for (QPushButton *b : {new_, edit_, delete_}) row1->addWidget(b);
    row1->addStretch();
    for (QPushButton *b : {load_, unload_, unloadAll_}) row2->addWidget(b);
    row2->addStretch();
    layout->addLayout(row1);
    layout->addLayout(row2);

    connect(new_, &QPushButton::clicked, this, [this] { edit(QString()); });
    connect(edit_, &QPushButton::clicked, this, [this] { edit(selectedName()); });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this] { edit(selectedName()); });
    connect(delete_, &QPushButton::clicked, this, [this] { remove(); });
    connect(load_, &QPushButton::clicked, this, [this] { load(selectedName()); });
    connect(unload_, &QPushButton::clicked, this, [this] { unload(selectedName()); });
    connect(unloadAll_, &QPushButton::clicked, this, [this] { unloadAll(); });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { updateButtons(); });
    connect(&refresh_, &QTimer::timeout, this, [this] { refreshStatus(); });
    refresh_.start(1000);
    reload();
}

void QuickTab::reload() {
    QString keep = selectedName();
    table_->setRowCount(0);
    for (const std::string &name : quickSetNames()) {
        QuickSet set;
        std::string error;
        bool ok = readQuickSet(name, set, error);
        int r = table_->rowCount();
        table_->insertRow(r);
        auto put = [&](int col, const QString &text) { table_->setItem(r, col, new QTableWidgetItem(text)); };
        put(Name, QString::fromStdString(name));
        put(Points, ok ? QString::number(set.points.size()) : "(cannot read)");
        put(Hotkey, ok && set.hotkey >= 0 ? QString::fromStdString(keyName(set.hotkey)) : "");
        QString gap = QString("%1 ms").arg(set.gapUs / 1000.0, 0, 'f', 1);
        if (set.gameFps > 0) gap += QString(" (game, %1 FPS)").arg(set.gameFps);
        put(Gap, ok ? gap : "");
        put(Status, "");
        if (QString::fromStdString(name) == keep) table_->selectRow(r);
    }
    refreshStatus();
}

void QuickTab::refreshStatus() {
    for (int r = 0; r < table_->rowCount(); ++r) {
        bool running = loadedPid(quickPidName(table_->item(r, Name)->text().toStdString())) != 0;
        QTableWidgetItem *item = table_->item(r, Status);
        item->setText(running ? "Running" : "Stopped");
        item->setForeground(running ? QColor("#a6e3a1") : QColor("#a6adc8"));
    }
    updateButtons();
}

QString QuickTab::selectedName() const {
    auto rows = table_->selectionModel()->selectedRows();
    return rows.isEmpty() ? QString() : table_->item(rows.first().row(), Name)->text();
}

void QuickTab::updateButtons() {
    QString name = selectedName();
    bool running = !name.isEmpty() && loadedPid(quickPidName(name.toStdString())) != 0;
    edit_->setEnabled(!name.isEmpty());
    delete_->setEnabled(!name.isEmpty());
    load_->setEnabled(!name.isEmpty() && !running);
    unload_->setEnabled(running);
    unloadAll_->setEnabled(!loadedQuickNames().empty());
}

void QuickTab::edit(const QString &name) {
    QuickSet set;
    std::string error;
    if (!name.isEmpty() && !readQuickSet(name.toStdString(), set, error)) set = QuickSet();
    QString typed = name, saved;
    for (;;) {
        QuickDialog dialog(name, typed, set, input_, this);
        int result = dialog.exec();
        set = dialog.set();
        typed = dialog.name();
        if (result == QuickDialog::Pick) {
            // The window out of the way while the points are picked; the
            // dialog then opens again with them (Esc keeps the old ones).
            window()->hide();
            pickPoints(set);
            window()->show();
            continue;
        }
        if (result != QDialog::Accepted) return;
        saved = typed;
        break;
    }
    const auto names = quickSetNames();
    if (name.isEmpty() && std::find(names.begin(), names.end(), saved.toStdString()) != names.end() &&
        QMessageBox::question(this, "Quick click", "'" + saved + "' already exists. Replace it?") != QMessageBox::Yes)
        return;
    if (!writeQuickSet(saved.toStdString(), set)) {
        QMessageBox::warning(this, "Quick click", "Could not save '" + saved + "'.");
        return;
    }
    reload();
    if (loadedPid(quickPidName(saved.toStdString())) &&
        QMessageBox::question(this, "Quick click",
                              "'" + saved + "' is running with the old points. Reload it now?") == QMessageBox::Yes) {
        unload(saved);
        load(saved);
    }
}

void QuickTab::remove() {
    QString name = selectedName();
    if (name.isEmpty()) return;
    if (QMessageBox::question(this, "Quick click", "Delete '" + name + "'?") != QMessageBox::Yes) return;
    unload(name);
    if (!deleteQuickSet(name.toStdString())) QMessageBox::warning(this, "Quick click", "Could not delete '" + name + "'.");
    reload();
}

bool QuickTab::load(const QString &name) {
    if (name.isEmpty()) return false;
    // A fresh process, like the command (catberry -quickload).
    QProcess process;
    QString self = QCoreApplication::applicationFilePath();
    process.start(QFileInfo::exists(self) ? self : QString("catberry-mtools"), {"-quickload", name});
    bool ok = process.waitForFinished(10000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (!ok) {
        QString why = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        why.remove(QRegularExpression("^catberry: "));
        QMessageBox::warning(this, "Quick click",
                             "Could not load '" + name + "'" + (why.isEmpty() ? QString(".") : ":\n" + why));
    }
    refreshStatus();
    return ok;
}

void QuickTab::unload(const QString &name) {
    if (name.isEmpty()) return;
    std::string pidName = quickPidName(name.toStdString());
    if (long pid = loadedPid(pidName)) {
        stopProcess(pid, pidName);
        forgetLoaded(pidName);
    }
    refreshStatus();
}

void QuickTab::unloadAll() {
    for (const std::string &name : loadedQuickNames()) unload(QString::fromStdString(name));
}
