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
// Per-user runtime files (pid files of loaded configs). Created on demand.
fs::path runtimeDir();

// Sleeps until `deadline` with sub-millisecond precision (plain waits on
// Windows only tick every 15.6 ms), returning early once `cancelled()`.
void sleepUntil(Clock::time_point deadline, const std::function<bool()> &cancelled);

long currentPid();
