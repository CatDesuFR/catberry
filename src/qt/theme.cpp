#include "theme.h"

const char *catberryStyleSheet() {
    return R"(
    QWidget { background: #1e1e2e; color: #cdd6f4; font-size: 10pt; }
    QTabWidget::pane { border: 1px solid #45475a; border-radius: 6px; top: -1px; }
    QTabBar::tab { background: #181825; padding: 6px 16px; border: 1px solid #45475a; border-bottom: none;
                   border-top-left-radius: 6px; border-top-right-radius: 6px; margin-right: 2px; }
    QTabBar::tab:selected { background: #313244; color: #89b4fa; }
    QPushButton { background: #313244; border: 1px solid #45475a; border-radius: 6px; padding: 6px 14px; }
    QPushButton:hover { background: #45475a; }
    QPushButton:checked, QPushButton#accent { background: #89b4fa; color: #1e1e2e; border: none; }
    QPushButton#go { background: #a6e3a1; color: #1e1e2e; border: none; font-weight: bold; }
    QPushButton#stop { background: #f38ba8; color: #1e1e2e; border: none; font-weight: bold; }
    QPushButton:disabled, QPushButton#go:disabled, QPushButton#stop:disabled, QPushButton#accent:disabled {
        background: #313244; color: #6c7086; border: 1px solid #45475a; font-weight: normal; }
    QSpinBox, QDoubleSpinBox, QLineEdit { background: #181825; border: 1px solid #45475a; border-radius: 6px;
                                          padding: 4px 8px; selection-background-color: #89b4fa; }
    /* The spin boxes have no arrows (NoButtons): give them no room either. */
    QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { width: 0px; border: none; }
    QListWidget, QTableWidget { background: #181825; border: 1px solid #45475a; border-radius: 6px;
                                alternate-background-color: #1e1e2e; }
    QListWidget::item:selected, QTableWidget::item:selected { background: #313244; color: #89b4fa; }
    QHeaderView::section { background: #313244; border: none; padding: 4px 8px; }
    QLabel#dim { color: #a6adc8; }
    QFrame#problems { background: #181825; border: 1px solid #f38ba8; border-radius: 6px; }
    QFrame#problems QLabel { background: transparent; }
    QLabel#warn { color: #f38ba8; }
    QFrame#testArea { background: #181825; border: 1px dashed #45475a; border-radius: 6px; }
    )";
}
