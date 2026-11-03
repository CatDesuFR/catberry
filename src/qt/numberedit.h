// A number box: a text field that takes numbers from min to max (with `.`
// or `,`), and shows the value cleaned up when you leave it or press Enter.
// (Qt's spin boxes, styled and without arrows, draw their text 1 px wide.)
#pragma once

#include <QLineEdit>

class NumberEdit : public QLineEdit {
public:
    NumberEdit(double min, double max, int decimals, QWidget *parent = nullptr);
    double value() const { return value_; }
    void setValue(double value);

private:
    double min_, max_;
    int decimals_;
    double value_;
};
