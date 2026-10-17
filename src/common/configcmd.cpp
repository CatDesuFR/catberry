// catberry -config / -configload / -configunload: hotkey macros that run in
// the background, one small process per loaded config.

#include "macro.h"
#include "platform.h"

#include <cstdio>
#include <cstdlib>

int configSave(int argc, char **argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: catberry -config NAME KEYS HOTKEY COUNT\n"
                             "   eg: catberry -config EasyEdit E,LMB F6 1\n");
        return 2;
    }
    std::string name = argv[0], hotkeyText = argv[2], bad;
    MacroConfig cfg;
    cfg.keysText = argv[1];
    char *end = nullptr;
    long count = std::strtol(argv[3], &end, 10);

    if (!validName(name)) {
        std::fprintf(stderr, "catberry: config name may only use letters, digits, '_' and '-'\n");
        return 2;
    }
    if (!parseKeyList(cfg.keysText, cfg.keys, bad)) {
        std::fprintf(stderr, "catberry: unknown key '%s' (examples: E, F6, Space, Shift, LMB, RMB, ScrollUp)\n",
                     bad.c_str());
        return 2;
    }
    cfg.hotkey = parseKey(hotkeyText);
    if (cfg.hotkey < 0 || isWheel(cfg.hotkey)) {
        std::fprintf(stderr, "catberry: '%s' cannot be used as a hotkey\n", hotkeyText.c_str());
        return 2;
    }
    if (*end != '\0' || count < 1 || count > 1000000) {
        std::fprintf(stderr, "catberry: COUNT must be a whole number, 1 or more\n");
        return 2;
    }
    cfg.count = static_cast<int>(count);

    // Keep a hand-tuned delay when a config is redefined.
    MacroConfig old;
    std::string ignored;
    if (readConfig(name, old, ignored)) cfg.delayMs = old.delayMs;

    if (!writeConfig(name, cfg)) {
        std::fprintf(stderr, "catberry: cannot save config '%s'\n", name.c_str());
        return 1;
    }
    std::printf("Saved config '%s': %s runs %s, %d time(s).\n", name.c_str(), keyName(cfg.hotkey).c_str(),
                cfg.keysText.c_str(), cfg.count);
    if (loadedPid(name))
        std::printf("It is loaded with the old settings; reload with: catberry -configunload %s && catberry "
                    "-configload %s\n",
                    name.c_str(), name.c_str());
    else
        std::printf("Load it with: catberry -configload %s\n", name.c_str());
    return 0;
}

int configLoad(int argc, char **argv) {
    if (argc != 1) {
        std::fprintf(stderr, "usage: catberry -configload NAME\n");
        return 2;
    }
    std::string name = argv[0], error;
    MacroConfig cfg;
    if (!validName(name)) {
        std::fprintf(stderr, "catberry: invalid config name\n");
        return 1;
    }
    if (!readConfig(name, cfg, error)) {
        std::fprintf(stderr, "catberry: %s\n", error.c_str());
        return 1;
    }
    if (long pid = loadedPid(name)) {
        std::fprintf(stderr, "catberry: config '%s' is already loaded (pid %ld)\n", name.c_str(), pid);
        return 1;
    }
    if (!launchDaemon(name, cfg, error)) {
        std::fprintf(stderr, "catberry: %s\n", error.c_str());
        return 1;
    }
    std::printf("Loaded '%s': %s runs %s, %d time(s). Stop it with: catberry -configunload %s\n", name.c_str(),
                keyName(cfg.hotkey).c_str(), cfg.keysText.c_str(), cfg.count, name.c_str());
    return 0;
}

int configUnload(int argc, char **argv) {
    if (argc > 1) {
        std::fprintf(stderr, "usage: catberry -configunload [NAME]\n");
        return 2;
    }
    std::vector<std::string> names = argc == 1 ? std::vector<std::string>{argv[0]} : loadedNames();
    int stopped = 0;
    for (const std::string &name : names) {
        if (!validName(name)) continue;
        long pid = loadedPid(name);
        if (!pid) continue;
        stopProcess(pid, name);
        forgetLoaded(name);
        std::printf("Unloaded '%s'\n", name.c_str());
        ++stopped;
    }
    if (stopped) return 0;
    if (argc == 1) {
        std::fprintf(stderr, "catberry: config '%s' is not loaded\n", argv[0]);
        return 1;
    }
    std::printf("No configs are loaded.\n");
    return 0;
}
