// catberry-roofce - Roof Carton Engine, the catberry wallpaper manager.
// Usually run through the catberry launcher as `catberry roofce ...`.

#include "mainwindow.h"
#include "wallpaper.h"

#include <QApplication>

#include <cstdio>
#include <string>

static void usage(FILE *out) {
    std::fprintf(out, "usage:\n"
                      "  catberry roofce gui     open the wallpaper manager\n"
                      "  catberry roofce start   show the saved wallpaper\n"
                      "  catberry roofce stop    stop showing the wallpaper\n"
                      "  catberry roofce -v      print the version\n"
                      "  catberry roofce -help   show this help\n");
}

int main(int argc, char **argv) {
    std::string command = argc > 1 ? argv[1] : "";
    if ((command == "-v" || command == "--version") && argc == 2) {
        std::printf("catberry roofce %s\n", ROOFCE_VERSION);
        return 0;
    }
    if (command == "--wallpaper") return runWallpaper(argc, argv);
    if (command == "gui" || command == "start" || command == "stop") {
        QApplication app(argc, argv);
        app.setApplicationName("catberry-roofce");
        app.setDesktopFileName("catberry-roofce");
        if (command == "stop") {
            stopWallpaper();
            return 0;
        }
        if (command == "start") {
            QString error;
            if (showWallpaper(error)) return 0;
            std::fprintf(stderr, "catberry roofce: %s\n", qPrintable(error));
            return 1;
        }
        MainWindow window;
        window.show();
        return app.exec();
    }
    bool help = command == "-help" || command == "-h" || command == "--help";
    usage(help ? stdout : stderr);
    return help ? 0 : 2;
}
