// Background processes for loaded configs (catberry -configload).
#pragma once

#include "platform.h"

bool isCatberryProcess(long pid);
// Asks a loaded config to exit, killing it if it does not.
void stopProcess(long pid, const std::string &name);

struct MacroConfig;
// Starts config `name` in a background process that runs serveConfig().
bool launchDaemon(const std::string &name, const MacroConfig &cfg, std::string &error);
// Hidden `catberry-mtools --serve NAME` entry point, where launchDaemon needs one.
int serveMain(int argc, char **argv);

struct QuickSet;
// The same for a quick-click set (pid name "quick.<name>").
bool launchQuickDaemon(const std::string &name, const QuickSet &set, std::string &error);
// Hidden `catberry-mtools --serve-quick NAME` entry point (Windows).
int serveQuickMain(int argc, char **argv);

// Global key and mouse-button transitions for a background config.
class KeyWatcher {
public:
    // `name` identifies the config, for stop requests. `mouse`: the hotkey
    // is a mouse button. On failure returns null and sets `error`.
    static std::unique_ptr<KeyWatcher> create(const std::string &name, bool mouse, std::string &error);
    virtual ~KeyWatcher() = default;
    // Waits up to `timeoutMs` (-1: no limit) for the next transition (never
    // autorepeat, never our own output). False on timeout or once stopped.
    virtual bool next(InputEvent &ev, int timeoutMs) = 0;
    virtual bool stopRequested() const = 0;
};
