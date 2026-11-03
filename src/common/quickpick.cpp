// catberry -quickpick NAME HOTKEY [GAP_MS]: pick the points of a set.
#include "daemon.h"
#include "macro.h"
#include "picker.h"
#include "quickclick.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

int quickPick(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "usage: catberry -quickpick NAME HOTKEY [GAP_MS]\n"
                             "   eg: catberry -quickpick Farm F7 0.1\n");
        return 2;
    }
    std::string name = argv[0], error;
    if (!validName(name)) {
        std::fprintf(stderr, "catberry: the name may only use letters, digits, '_' and '-'\n");
        return 2;
    }
    int hotkey = parseKey(argv[1]);
    if (hotkey < 0 || isWheel(hotkey)) {
        std::fprintf(stderr, "catberry: '%s' cannot be used as a hotkey\n", argv[1]);
        return 2;
    }
    QuickSet set;
    bool existed = readQuickSet(name, set, error); // its points are the starting picture
    set.hotkey = hotkey;
    if (argc == 3) {
        char *end = nullptr;
        double ms = std::strtod(argv[2], &end);
        long long us = std::llround(ms * 1000);
        if (*end != '\0' || us < kMinGapUs || us > kMaxGapUs) {
            std::fprintf(stderr, "catberry: GAP_MS must be between 0.1 and 1000\n");
            return 2;
        }
        set.gapUs = int(us);
    }
    if (!pickPoints(set)) {
        std::printf("Cancelled; %s.\n", existed ? "the saved points are unchanged" : "nothing was saved");
        return 1;
    }
    if (!writeQuickSet(name, set)) {
        std::fprintf(stderr, "catberry: cannot save quick click '%s'\n", name.c_str());
        return 1;
    }
    std::printf("Saved quick click '%s': %s clicks %zu point(s), %.1f ms apart.\n", name.c_str(),
                keyName(set.hotkey).c_str(), set.points.size(), set.gapUs / 1000.0);
    if (loadedPid(quickPidName(name)))
        std::printf("It is loaded with the old points; reload with: catberry -quickunload %s && catberry -quickload "
                    "%s\n",
                    name.c_str(), name.c_str());
    else
        std::printf("Load it with: catberry -quickload %s\n", name.c_str());
    return 0;
}
