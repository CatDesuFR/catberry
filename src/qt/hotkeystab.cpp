#include "hotkeystab.h"

#include "../common/daemon.h"
#include "../common/macro.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <algorithm>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "configdialog.h"

namespace {
enum Column { Name, Keys, Hotkey, Count, Delay, Status, ColumnCount };
} // namespace

HotkeysTab::HotkeysTab(GlobalInput *input, QWidget *parent) : QWidget(parent), input_(input) {
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel("Hotkey macros run in the background, also after this window is closed: "
                             "pressing the hotkey presses the keys in order.",
                             this);
    intro->setObjectName("dim");
    intro->setWordWrap(true);
    layout->addWidget(intro);

    table_ = new QTableWidget(0, ColumnCount, this);
    table_->setHorizontalHeaderLabels({"Name", "Keys", "Hotkey", "Times", "Hold", "Status"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(Keys, QHeaderView::Stretch);
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

void HotkeysTab::reload() {
    QString keep = selectedName();
    table_->setRowCount(0);
    for (const std::string &name : configNames()) {
        MacroConfig cfg;
        std::string error;
        bool ok = readConfig(name, cfg, error);
        int r = table_->rowCount();
        table_->insertRow(r);
        auto set = [&](int col, const QString &text) { table_->setItem(r, col, new QTableWidgetItem(text)); };
        set(Name, QString::fromStdString(name));
        set(Keys, ok ? QString::fromStdString(cfg.keysText) : "(cannot read: " + QString::fromStdString(error) + ")");
        set(Hotkey, ok ? QString::fromStdString(keyName(cfg.hotkey)) : "");
        set(Count, ok ? QString::number(cfg.count) : "");
        set(Delay, ok ? QString("%1 ms").arg(cfg.delayMs) : "");
        set(Status, "");
        if (QString::fromStdString(name) == keep) table_->selectRow(r);
    }
    refreshStatus();
}

void HotkeysTab::refreshStatus() {
    for (int r = 0; r < table_->rowCount(); ++r) {
        bool running = loadedPid(table_->item(r, Name)->text().toStdString()) != 0;
        QTableWidgetItem *item = table_->item(r, Status);
        item->setText(running ? "Running" : "Stopped");
        item->setForeground(running ? QColor("#a6e3a1") : QColor("#a6adc8"));
    }
    updateButtons();
}

QString HotkeysTab::selectedName() const {
    auto rows = table_->selectionModel()->selectedRows();
    return rows.isEmpty() ? QString() : table_->item(rows.first().row(), Name)->text();
}

void HotkeysTab::updateButtons() {
    QString name = selectedName();
    bool running = !name.isEmpty() && loadedPid(name.toStdString()) != 0;
    edit_->setEnabled(!name.isEmpty());
    delete_->setEnabled(!name.isEmpty());
    load_->setEnabled(!name.isEmpty() && !running);
    unload_->setEnabled(running);
    unloadAll_->setEnabled(!loadedNames().empty());
}

void HotkeysTab::edit(const QString &name) {
    MacroConfig cfg;
    std::string error;
    if (!name.isEmpty() && !readConfig(name.toStdString(), cfg, error)) cfg = MacroConfig();
    ConfigDialog dialog(name, cfg, input_, this);
    if (dialog.exec() != QDialog::Accepted) return;
    QString saved = dialog.name();
    const auto names = configNames();
    if (name.isEmpty() && std::find(names.begin(), names.end(), saved.toStdString()) != names.end() &&
        QMessageBox::question(this, "Hotkey macro", "'" + saved + "' already exists. Replace it?") != QMessageBox::Yes)
        return;
    if (!writeConfig(saved.toStdString(), dialog.config())) {
        QMessageBox::warning(this, "Hotkey macro", "Could not save '" + saved + "'.");
        return;
    }
    reload();
    if (loadedPid(saved.toStdString()) &&
        QMessageBox::question(this, "Hotkey macro",
                              "'" + saved + "' is running with the old settings. Reload it now?") == QMessageBox::Yes) {
        unload(saved);
        load(saved);
    }
}

void HotkeysTab::remove() {
    QString name = selectedName();
    if (name.isEmpty()) return;
    if (QMessageBox::question(this, "Hotkey macro", "Delete '" + name + "'?") != QMessageBox::Yes) return;
    unload(name);
    if (!deleteConfig(name.toStdString())) QMessageBox::warning(this, "Hotkey macro", "Could not delete '" + name + "'.");
    reload();
}

bool HotkeysTab::load(const QString &name) {
    if (name.isEmpty()) return false;
    // A fresh process, like the command (catberry -configload): it starts
    // the background config itself.
    QProcess process;
    // After an update while the window is open, this program's own file is
    // gone; the new one is on PATH.
    QString self = QCoreApplication::applicationFilePath();
    process.start(QFileInfo::exists(self) ? self : QString("catberry-mtools"), {"-configload", name});
    bool ok = process.waitForFinished(10000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (!ok) {
        QString why = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        why.remove(QRegularExpression("^catberry: "));
        QMessageBox::warning(this, "Hotkey macro",
                             "Could not load '" + name + "'" + (why.isEmpty() ? QString(".") : ":\n" + why));
    }
    refreshStatus();
    return ok;
}

void HotkeysTab::unload(const QString &name) {
    if (name.isEmpty()) return;
    if (long pid = loadedPid(name.toStdString())) {
        stopProcess(pid, name.toStdString());
        forgetLoaded(name.toStdString());
    }
    refreshStatus();
}

void HotkeysTab::unloadAll() {
    for (const std::string &name : loadedNames()) unload(QString::fromStdString(name));
}
