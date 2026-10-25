// Paths and timing on Linux.

#include "../common/platform.h"
#include "linux.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <thread>

fs::path configDir() {
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    const char *home = std::getenv("HOME");
    fs::path dir = (xdg && *xdg) ? fs::path(xdg) : fs::path(home ? home : ".") / ".config";
    dir /= "catberry";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

fs::path runtimeDir() {
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    fs::path dir = (runtime && *runtime) ? fs::path(runtime) / "catberry"
                                         : fs::path("/tmp") / ("catberry-" + std::to_string(getuid()));
    mkdir(dir.c_str(), 0700);
    return dir;
}

void sleepUntil(Clock::time_point deadline, const std::function<bool()> &cancelled) {
    // Short slices so a cancel is noticed quickly.
    while (!cancelled()) {
        auto now = Clock::now();
        if (now >= deadline) return;
        std::this_thread::sleep_until(std::min(deadline, now + std::chrono::milliseconds(10)));
    }
}

long currentPid() { return getpid(); }
