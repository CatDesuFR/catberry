// Background processes for loaded configs on Linux: a forked child that
// watches /dev/input for the hotkey.

#include "../common/daemon.h"
#include "../common/macro.h"
#include "../common/quickclick.h"
#include "linux.h"

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cstdio>
#include <deque>
#include <functional>
#include <fstream>

namespace {

volatile sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

class EvdevWatcher : public KeyWatcher {
public:
    bool available() const { return input_.available(); }

    bool next(InputEvent &ev, int timeoutMs) override {
        auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? 0 : timeoutMs);
        for (;;) {
            if (!pending_.empty()) {
                ev = pending_.front();
                pending_.pop_front();
                return true;
            }
            if (g_stop) return false;
            // Wake up at least every 3 s to pick up newly plugged keyboards.
            int wait = 3000;
            long long left = 1;
            if (timeoutMs >= 0) {
                left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
                wait = static_cast<int>(std::max<long long>(0, std::min<long long>(left, wait)));
            }
            std::vector<pollfd> fds;
            input_.addPollFds(fds);
            int ready = poll(fds.data(), fds.size(), wait);
            if (ready == 0 && wait > 0) input_.rescan();
            for (const InputEvent &e : input_.read()) pending_.push_back(e);
            // Out of time: one look at what was pending was all (timeout 0 too).
            if (pending_.empty() && timeoutMs >= 0 && left <= 0) return false;
        }
    }

    bool stopRequested() const override { return g_stop; }

private:
    EvdevInput input_;
    std::deque<InputEvent> pending_;
};

} // namespace

void installStopSignals() {
    struct sigaction action {};
    action.sa_handler = onSignal;
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGHUP, &action, nullptr);
}

std::unique_ptr<KeyWatcher> KeyWatcher::create(const std::string &, bool, std::string &error) {
    std::unique_ptr<EvdevWatcher> watcher(new EvdevWatcher);
    if (!watcher->available()) {
        error = std::string("cannot read the keyboard to watch for the hotkey.\n"
                            "Run this once, then log out and back in:\n  ") +
                kInputGroupCommand;
        return nullptr;
    }
    return watcher;
}

bool isCatberryProcess(long pid) {
    std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
    std::string exe;
    return comm >> exe && exe == "catberry-mtools";
}

void stopProcess(long pid, const std::string &) { kill(static_cast<pid_t>(pid), SIGTERM); }

namespace {

// Forks a background process that runs `serve`; it reports through a pipe
// whether it got its devices (an empty message means it did).
bool launchServe(const std::string &pidName,
                 const std::function<int(const std::function<void(const std::string &)> &)> &serve,
                 std::string &error) {
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
        int code = serve([&](const std::string &err) {
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
    if (message.empty() && loadedPid(pidName)) return true;
    error = message.empty() ? "the background process failed to start" : message;
    return false;
}

} // namespace

bool launchDaemon(const std::string &name, const MacroConfig &cfg, std::string &error) {
    return launchServe(
        name, [&](const std::function<void(const std::string &)> &ready) { return serveConfig(name, cfg, ready); },
        error);
}

bool launchQuickDaemon(const std::string &name, const QuickSet &set, std::string &error) {
    return launchServe(
        quickPidName(name),
        [&](const std::function<void(const std::string &)> &ready) { return serveQuickSet(name, set, ready); },
        error);
}

int serveMain(int, char **) {
    std::fprintf(stderr, "catberry: --serve is only used on Windows\n");
    return 2;
}

int serveQuickMain(int, char **) {
    std::fprintf(stderr, "catberry: --serve-quick is only used on Windows\n");
    return 2;
}
