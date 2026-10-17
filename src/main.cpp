// catberry - autoclicker (ac), macro recorder (mtools) and hotkey macro
// configs behind one command.

#include <cstdio>
#include <string>

static const char *const kAcVersion = "1.0.2";
static const char *const kMtoolsVersion = "1.0.0";

int acGui(int argc, char **argv);
int mtoolsGui();
int configSave(int argc, char **argv);
int configLoad(int argc, char **argv);
int configUnload(int argc, char **argv);
int serveMain(int argc, char **argv);

static void usage(FILE *out) {
    std::fprintf(out,
                 "usage:\n"
                 "  catberry ac gui                 open the autoclicker\n"
                 "  catberry ac -v                  print the autoclicker version\n"
                 "  catberry mtools gui             open the macro recorder\n"
                 "  catberry mtools -v              print the macro recorder version\n"
                 "  catberry -config NAME KEYS HOTKEY COUNT\n"
                 "                                  save a hotkey macro, eg: -config EasyEdit E,LMB F6 1\n"
                 "  catberry -configload NAME       start a saved macro in the background\n"
                 "  catberry -configunload [NAME]   stop one loaded macro, or all of them\n");
}

int main(int argc, char **argv) {
    std::string command = argc > 1 ? argv[1] : "";
    std::string sub = argc > 2 ? argv[2] : "";

    if (command == "-config") return configSave(argc - 2, argv + 2);
    if (command == "-configload") return configLoad(argc - 2, argv + 2);
    if (command == "-configunload") return configUnload(argc - 2, argv + 2);
    if (command == "--serve") return serveMain(argc - 2, argv + 2);
    if (command == "ac" && sub == "gui") return acGui(argc - 3, argv + 3);
    if (command == "mtools" && sub == "gui" && argc == 3) return mtoolsGui();
    if ((command == "ac" || command == "mtools") && sub == "-v" && argc == 3) {
        std::printf("catberry %s %s\n", command.c_str(), command == "ac" ? kAcVersion : kMtoolsVersion);
        return 0;
    }
    bool help = command == "-h" || command == "--help";
    usage(help ? stdout : stderr);
    return help ? 0 : 2;
}
