// catberry-ac - the catberry autoclicker. Usually run through the catberry
// launcher as `catberry ac ...`.

#include <cstdio>
#include <string>

static const char *const kVersion = "2.0.1";

int acGui(int argc, char **argv);

static void usage(FILE *out) {
    std::fprintf(out, "usage:\n"
                      "  catberry ac gui     open the autoclicker\n"
                      "  catberry ac -v      print the autoclicker version\n"
                      "  catberry ac -help   show this help\n");
}

int main(int argc, char **argv) {
    std::string command = argc > 1 ? argv[1] : "";
    if (command == "gui") return acGui(argc - 2, argv + 2);
    if ((command == "-v" || command == "--version") && argc == 2) {
        std::printf("catberry ac %s\n", kVersion);
        return 0;
    }
    bool help = command == "-help" || command == "-h" || command == "--help";
    usage(help ? stdout : stderr);
    return help ? 0 : 2;
}
