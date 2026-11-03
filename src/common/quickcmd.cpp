// catberry -quickload / -quickunload (and -quickpick, in quickpick.cpp):
// quick-click sets in background processes, like the hotkey macros.
#include "daemon.h"
#include "macro.h"
#include "quickclick.h"

#include <cstdio>

int quickLoad(int argc, char **argv) {
    if (argc != 1) {
        std::fprintf(stderr, "usage: catberry -quickload NAME\n");
        return 2;
    }
    std::string name = argv[0], error;
    QuickSet set;
    if (!readQuickSet(name, set, error)) {
        std::fprintf(stderr, "catberry: %s\n", error.c_str());
        return 1;
    }
    if (set.points.empty() || set.hotkey < 0) {
        std::fprintf(stderr, "catberry: quick click '%s' has no points or no hotkey; pick them with: catberry "
                             "-quickpick %s HOTKEY\n",
                     name.c_str(), name.c_str());
        return 1;
    }
    if (long pid = loadedPid(quickPidName(name))) {
        std::fprintf(stderr, "catberry: quick click '%s' is already loaded (pid %ld)\n", name.c_str(), pid);
        return 1;
    }
    if (!launchQuickDaemon(name, set, error)) {
        std::fprintf(stderr, "catberry: %s\n", error.c_str());
        return 1;
    }
    std::printf("Loaded quick click '%s': %s clicks %zu point(s). Stop it with: catberry -quickunload %s\n",
                name.c_str(), keyName(set.hotkey).c_str(), set.points.size(), name.c_str());
    return 0;
}

int quickUnload(int argc, char **argv) {
    if (argc > 1) {
        std::fprintf(stderr, "usage: catberry -quickunload [NAME]\n");
        return 2;
    }
    std::vector<std::string> names;
    if (argc == 1) {
        names.push_back(argv[0]);
    } else {
        const std::string prefix = quickPidName("");
        for (const std::string &pidName : loadedNames())
            if (pidName.compare(0, prefix.size(), prefix) == 0) names.push_back(pidName.substr(prefix.size()));
    }
    int stopped = 0;
    for (const std::string &name : names) {
        if (!validName(name)) continue;
        long pid = loadedPid(quickPidName(name));
        if (!pid) continue;
        stopProcess(pid, quickPidName(name));
        forgetLoaded(quickPidName(name));
        std::printf("Unloaded quick click '%s'\n", name.c_str());
        ++stopped;
    }
    if (stopped) return 0;
    if (argc == 1) {
        std::fprintf(stderr, "catberry: quick click '%s' is not loaded\n", argv[0]);
        return 1;
    }
    std::printf("No quick clicks are loaded.\n");
    return 0;
}
