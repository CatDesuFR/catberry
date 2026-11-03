// The catberry look (the same colours as roofce's window).
#pragma once

#include <QColor>

namespace theme {
const QColor kBg("#1e1e2e"), kPanel("#181825"), kButton("#313244"), kBorder("#45475a"), kText("#cdd6f4"),
    kDim("#a6adc8"), kAccent("#89b4fa"), kGo("#a6e3a1"), kStop("#f38ba8");
} // namespace theme

// For QApplication::setStyleSheet. Buttons can be given the object names
// "go", "stop" and "accent" for the coloured kinds.
const char *catberryStyleSheet();
