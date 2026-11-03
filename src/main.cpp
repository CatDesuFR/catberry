// catberry-mtools - the catberry macro recorder and hotkey macro configs.
// Usually run through the catberry launcher as `catberry mtools ...` and
// `catberry -config ...`.

#include <cstdio>
#include <cstdlib>
#include <string>

static const char *const kVersion = "1.1.0";

#ifdef _WIN32
int mtoolsGui();
#else
int mtoolsGui(int quitAfterMs);
#endif
int configSave(int argc, char **argv);
int configLoad(int argc, char **argv);
int configUnload(int argc, char **argv);
int serveMain(int argc, char **argv);

static void usage(FILE *out) {
    std::fprintf(out,
                 "usage:\n"
                 "  catberry mtools gui             open the macro recorder\n"
                 "  catberry mtools -v              print the macro recorder version\n"
                 "  catberry mtools -help           show this help\n"
                 "  catberry -config NAME KEYS HOTKEY COUNT\n"
                 "                                  save a hotkey macro, eg: -config EasyEdit E,LMB F6 1\n"
                 "  catberry -configload NAME       start a saved macro in the background\n"
                 "  catberry -configunload [NAME]   stop one loaded macro, or all of them\n");
}

int main(int argc, char **argv) {
    std::string command = argc > 1 ? argv[1] : "";
#ifdef _WIN32
    if (command == "gui" && argc == 2) return mtoolsGui();
#else
    // --quit-after MS closes the window after MS milliseconds (for tests).
    if (command == "gui" && argc == 2) return mtoolsGui(0);
    if (command == "gui" && argc == 4 && std::string(argv[2]) == "--quit-after") return mtoolsGui(std::atoi(argv[3]));
#endif
    if (command == "-config") return configSave(argc - 2, argv + 2);
    if (command == "-configload") return configLoad(argc - 2, argv + 2);
    if (command == "-configunload") return configUnload(argc - 2, argv + 2);
    if (command == "--serve") return serveMain(argc - 2, argv + 2);
    if ((command == "-v" || command == "--version") && argc == 2) {
        std::printf("catberry mtools %s\n", kVersion);
        return 0;
    }
    bool help = command == "-help" || command == "-h" || command == "--help";
    usage(help ? stdout : stderr);
    return help ? 0 : 2;
}
