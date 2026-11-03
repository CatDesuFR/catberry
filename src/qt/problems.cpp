#include "problems.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

ProblemsBanner::ProblemsBanner(QWidget *parent) : QFrame(parent) {
    setObjectName("problems");
    auto *layout = new QVBoxLayout(this);
    input_ = problem("sudo usermod -aG input $USER", inputText_);
    output_ = problem("catberry install catberry", outputText_);
    outputText_->setText("Clicking and typing are not possible: this user cannot use /dev/uinput. "
                         "Install the catberry launcher package (it adds the rule), then log out and back in:");
    layout->addWidget(input_);
    layout->addWidget(output_);
    input_->hide();
    output_->hide();
    hide();
}

QWidget *ProblemsBanner::problem(const QString &command, QLabel *&text) {
    auto *box = new QWidget(this);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    text = new QLabel(box);
    text->setObjectName("warn");
    text->setWordWrap(true);
    auto *row = new QHBoxLayout;
    auto *line = new QLineEdit(command, box);
    line->setReadOnly(true);
    auto *copy = new QPushButton("Copy", box);
    QObject::connect(copy, &QPushButton::clicked, box, [command] { QApplication::clipboard()->setText(command); });
    row->addWidget(line, 1);
    row->addWidget(copy);
    layout->addWidget(text);
    layout->addLayout(row);
    return box;
}

void ProblemsBanner::setInputMissing(bool missing, const QString &what) {
    inputText_->setText(what + ". To fix that, run this, then log out and back in:");
    input_->setVisible(missing);
    update();
}

void ProblemsBanner::setOutputMissing(bool missing) {
    output_->setVisible(missing);
    update();
}

void ProblemsBanner::update() { setVisible(!input_->isHidden() || !output_->isHidden()); }
