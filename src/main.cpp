// catberry-roofce - Roof Carton Engine, the catberry wallpaper manager.
// Usually run through the catberry launcher as `catberry roofce ...`.

#include "mainwindow.h"
#include "wallpaper.h"

#include <QApplication>

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

// Qt's video support and the graphics drivers print start-up notes that
// mean nothing to the user.
static void quiet() {
    qputenv("QT_LOGGING_RULES", "*.debug=false;*.info=false;*.warning=false");
    qputenv("LIBVA_MESSAGING_LEVEL", "0");
}

// Hands the terminal back straight away: the window carries on in the
// background, with its output going nowhere.
static void detach() {
    pid_t pid = fork();
    if (pid > 0) _exit(0);
    if (pid < 0) return; // keep running in the foreground instead
    setsid();
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, 0);
        dup2(devnull, 1);
        dup2(devnull, 2);
        if (devnull > 2) close(devnull);
    }
}

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
    quiet();
    if (command == "--wallpaper") return runWallpaper(argc, argv);
    if (command == "gui" || command == "start" || command == "stop") {
        if (command == "gui") {
            if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("DISPLAY")) {
                std::fprintf(stderr, "catberry roofce: no desktop session to open the window on\n");
                return 1;
            }
            detach();
        }
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
