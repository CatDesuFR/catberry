// What each platform (src/linux, src/windows) provides to the shared code.
#pragma once

#include "keys.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace fs = std::filesystem;

// Per-user settings, macros and configs. Created on demand.
fs::path configDir();
// Pid files of loaded configs. Created on demand.
fs::path runtimeDir();

// Sleeps until `deadline` with sub-millisecond precision (plain waits on
// Windows only tick every 15.6 ms), returning early once `cancelled()`.
void sleepUntil(Clock::time_point deadline, const std::function<bool()> &cancelled);

long currentPid();
bool isCatberryProcess(long pid);
// Asks a loaded config to exit, killing it if it does not.
void stopProcess(long pid, const std::string &name);

struct MacroConfig;
// Starts config `name` in a background process that runs serveConfig().
bool launchDaemon(const std::string &name, const MacroConfig &cfg, std::string &error);
// Hidden `catberry --serve NAME` entry point, where launchDaemon needs one.
int serveMain(int argc, char **argv);

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
