#include "numberedit.h"

#include <algorithm>
#include <cmath>

NumberEdit::NumberEdit(double min, double max, int decimals, QWidget *parent)
    : QLineEdit(parent), min_(min), max_(max), decimals_(decimals), value_(min) {
    setValue(min);
    connect(this, &QLineEdit::textEdited, this, [this](const QString &text) {
        bool ok = false;
        double v = QString(text).replace(',', '.').toDouble(&ok);
        if (ok && v >= min_ && v <= max_) value_ = v;
    });
    connect(this, &QLineEdit::editingFinished, this, [this] { setValue(value_); });
}

void NumberEdit::setValue(double value) {
    double scale = std::pow(10.0, decimals_);
    value_ = std::clamp(std::round(value * scale) / scale, min_, max_);
    setText(QString::number(value_, 'f', decimals_));
}
