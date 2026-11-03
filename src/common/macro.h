// Recorded macros (catberry mtools) and hotkey configs (catberry -config).
#pragma once

#include "keys.h"

#include <functional>
#include <string>
#include <vector>

struct MacroEvent {
    long long us; // offset from the start of the recording
    int code;
    bool down;
};

struct Macro {
    std::vector<MacroEvent> events;
    long long durationUs = 0;
};

bool saveMacro(const std::string &name, const Macro &macro);
bool loadMacro(const std::string &name, Macro &macro);
// Saved macros, by name, sorted.
std::vector<std::string> macroNames();
bool deleteMacro(const std::string &name);
// False when `from` does not exist, `to` already does, or `to` is not a valid name.
bool renameMacro(const std::string &from, const std::string &to);

struct MacroConfig {
    std::string keysText;
    std::vector<int> keys;
    int hotkey = -1;
    int count = 1;
    // How long each key is held, and the pause before the next one.
    int delayMs = 15;
};

// Letters, digits, '_' and '-' only, since names become file names.
bool validName(const std::string &name);
// On failure `bad` names the entry that was not understood.
bool parseKeyList(const std::string &text, std::vector<int> &keys, std::string &bad);
bool readConfig(const std::string &name, MacroConfig &cfg, std::string &error);
bool writeConfig(const std::string &name, const MacroConfig &cfg);
// Saved hotkey configs, by name, sorted.
std::vector<std::string> configNames();
bool deleteConfig(const std::string &name);

// The pid of the process serving config `name`, or 0. Clears stale pid files.
long loadedPid(const std::string &name);
// Names that have a pid file, live or stale.
std::vector<std::string> loadedNames();
void forgetLoaded(const std::string &name);
// Runs a loaded config until asked to stop. `ready` is called once, with an
// empty string when setup worked or the reason it did not.
int serveConfig(const std::string &name, const MacroConfig &cfg,
                const std::function<void(const std::string &error)> &ready);
