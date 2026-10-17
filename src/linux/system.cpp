// Paths and background processes on Linux.

#include "../common/macro.h"
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

bool isCatberryProcess(long pid) {
    std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
    std::string exe;
    return comm >> exe && exe == "catberry";
}

void stopProcess(long pid, const std::string &) { kill(static_cast<pid_t>(pid), SIGTERM); }

bool launchDaemon(const std::string &name, const MacroConfig &cfg, std::string &error) {
    // The child reports through this pipe whether it got its devices: an
    // empty message means success.
    int report[2];
    if (pipe(report) < 0) {
        error = "cannot start the background process";
        return false;
    }
    pid_t child = fork();
    if (child < 0) {
        error = "cannot start the background process";
        return false;
    }

    if (child == 0) {
        close(report[0]);
        setsid();
        installStopSignals();
        int code = serveConfig(name, cfg, [&](const std::string &err) {
            if (write(report[1], err.data(), err.size()) < 0) {
            }
            close(report[1]);
            if (err.empty()) {
                int devnull = open("/dev/null", O_RDWR);
                dup2(devnull, 0);
                dup2(devnull, 1);
                dup2(devnull, 2);
            }
        });
        _exit(code);
    }

    close(report[1]);
    std::string message;
    char buf[256];
    ssize_t n;
    while ((n = read(report[0], buf, sizeof buf)) > 0) message.append(buf, n);
    close(report[0]);
    if (message.empty() && loadedPid(name)) return true;
    error = message.empty() ? "the background process failed to start" : message;
    return false;
}

int serveMain(int, char **) {
    std::fprintf(stderr, "catberry: --serve is only used on Windows\n");
    return 2;
}
