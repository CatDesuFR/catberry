// The red box explaining what does not work yet and the command that
// fixes it, with a Copy button. Hidden when there is nothing to say.
#pragma once

#include <QFrame>

class QLabel;
class QLineEdit;
class QWidget;

class ProblemsBanner : public QFrame {
public:
    explicit ProblemsBanner(QWidget *parent = nullptr);
    // No /dev/input: `what` says what that turns off, e.g. "The hotkey only
    // works while this window is focused".
    void setInputMissing(bool missing, const QString &what);
    // No /dev/uinput: nothing can be clicked or typed.
    void setOutputMissing(bool missing);

private:
    QWidget *problem(const QString &command, QLabel *&text);
    void update();

    QWidget *input_, *output_;
    QLabel *inputText_, *outputText_;
};
