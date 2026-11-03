// The Qt point picker (pickPoints in ../common/picker.h): one see-through
// full-screen window per screen, all sharing the same points.
#pragma once

#include "../common/quickclick.h"

#include <QWidget>

#include <functional>

struct PickerState {
    std::vector<QuickPoint> points; // in desktop pixels
    ClickType type = ClickType::Left;
    QPoint origin; // the desktop's top-left corner in screen coordinates
    std::function<void(bool saved)> finish;
    std::function<void()> changed;
};

class PickerWindow : public QWidget {
public:
    PickerWindow(PickerState &state, QScreen *screen, bool showsBar);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    PickerState &state_;
    bool showsBar_;
};
