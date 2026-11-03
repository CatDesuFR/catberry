#include "configdialog.h"

#include "globalinput.h"
#include "keybutton.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QVBoxLayout>

ConfigDialog::ConfigDialog(const QString &name, const MacroConfig &cfg, GlobalInput *input, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(name.isEmpty() ? "New hotkey macro" : "Edit '" + name + "'");
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    name_ = new QLineEdit(name, this);
    name_->setValidator(new QRegularExpressionValidator(QRegularExpression("[A-Za-z0-9_-]{0,40}"), name_));
    name_->setPlaceholderText("e.g. EasyEdit");
    name_->setReadOnly(!name.isEmpty());
    keys_ = new QLineEdit(QString::fromStdString(cfg.keysText), this);
    keys_->setPlaceholderText("e.g. E,LMB");
    hotkey_ = new KeyButton(cfg.hotkey, this);
    hotkey_->setEmptyText("Click, then press a key");
    hotkey_->setAcceptsMouse(true);
    count_ = new QSpinBox(this);
    count_->setRange(1, 1000000);
    count_->setValue(cfg.count);
    count_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    delay_ = new QSpinBox(this);
    delay_->setRange(1, 1000);
    delay_->setSuffix(" ms");
    delay_->setValue(cfg.delayMs);
    delay_->setButtonSymbols(QAbstractSpinBox::NoButtons);

    form->addRow("Name", name_);
    form->addRow("Keys, in order", keys_);
    form->addRow("Hotkey", hotkey_);
    form->addRow("Times per press", count_);
    form->addRow("Hold / pause", delay_);
    layout->addLayout(form);

    auto *help = new QLabel("Keys: letters, digits, F1-F12, Space, Enter, Tab, Esc, Shift, Ctrl, Alt, "
                            "Up/Down/Left/Right, ..., and the mouse: LMB, RMB, MMB, MB4, MB5, ScrollUp, "
                            "ScrollDown. The hotkey can also be a side mouse button.",
                            this);
    help->setObjectName("dim");
    help->setWordWrap(true);
    layout->addWidget(help);
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
    connect(keys_, &QLineEdit::textChanged, this, [this] { check(); });
    connect(hotkey_, &KeyButton::changed, this, [this] { check(); });
    if (input) connect(input, &GlobalInput::input, this, [this](InputEvent ev) { hotkey_->offerGlobal(ev); });
    check();
    resize(440, sizeHint().height());
}

void ConfigDialog::check() {
    QString problem;
    std::vector<int> keys;
    std::string bad;
    if (!validName(name_->text().toStdString()))
        problem = "Give it a name (letters, digits, _ and -).";
    else if (keys_->text().trimmed().isEmpty())
        problem = "Type the keys it presses, separated by commas.";
    else if (!parseKeyList(keys_->text().trimmed().toStdString(), keys, bad))
        problem = "Unknown key '" + QString::fromStdString(bad) + "'.";
    else if (hotkey_->code() < 0)
        problem = "Choose the hotkey: click the button, then press a key.";
    problem_->setText(problem);
    ok_->setEnabled(problem.isEmpty());
}

QString ConfigDialog::name() const { return name_->text(); }

MacroConfig ConfigDialog::config() const {
    MacroConfig cfg;
    cfg.keysText = keys_->text().trimmed().toStdString();
    std::string bad;
    parseKeyList(cfg.keysText, cfg.keys, bad);
    cfg.hotkey = hotkey_->code();
    cfg.count = count_->value();
    cfg.delayMs = delay_->value();
    return cfg;
}
