#include "mtoolswindow.h"

#include "theme.h"

#include <QApplication>
#include <QTabWidget>
#include <QTimer>

MtoolsWindow::MtoolsWindow() {
    setWindowTitle("Catberry Macro Recorder");
    auto *tabs = new QTabWidget(this);
    tabs->addTab(new QWidget, "Recorder");
    tabs->addTab(new QWidget, "Hotkey macros");
    setCentralWidget(tabs);
    resize(520, 560);
}

MtoolsWindow::~MtoolsWindow() = default;

int mtoolsGui(int quitAfterMs) {
    int argc = 1;
    char name[] = "catberry-mtools";
    char *argv[] = {name, nullptr};
    QApplication app(argc, argv);
    app.setApplicationName("catberry-mtools");
    app.setDesktopFileName("catberry-mtools");
    app.setStyleSheet(catberryStyleSheet());
    MtoolsWindow window;
    window.show();
    if (quitAfterMs > 0) QTimer::singleShot(quitAfterMs, &app, &QApplication::quit);
    return app.exec();
}
