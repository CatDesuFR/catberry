// Global input through /dev/input, for the windows and background configs.

#include "../common/platform.h"
#include "linux.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <deque>

EvdevInput::~EvdevInput() {
    for (auto &entry : fds_) close(entry.second);
}

void EvdevInput::rescan() {
    DIR *dir = opendir("/dev/input");
    if (!dir) return;
    while (dirent *entry = readdir(dir)) {
        if (std::strncmp(entry->d_name, "event", 5) != 0) continue;
        std::string path = std::string("/dev/input/") + entry->d_name;
        if (fds_.count(path)) continue;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        unsigned long evbits = 0;
        input_id id{};
        bool wanted = ioctl(fd, EVIOCGBIT(0, sizeof evbits), &evbits) >= 0 && (evbits & (1UL << EV_KEY)) &&
                      ioctl(fd, EVIOCGID, &id) >= 0 &&
                      // Never listen to our own virtual devices: a macro
                      // that sends its own hotkey would retrigger itself.
                      !(id.vendor == kVendor && (id.product == kMouseProduct || id.product == kKeyboardProduct));
        if (!wanted) {
            close(fd);
            continue;
        }
        fds_[path] = fd;
    }
    closedir(dir);
}

void EvdevInput::addPollFds(std::vector<pollfd> &out) const {
    for (const auto &entry : fds_) out.push_back({entry.second, POLLIN, 0});
}

std::vector<InputEvent> EvdevInput::read() {
    std::vector<InputEvent> out;
    for (auto it = fds_.begin(); it != fds_.end();) {
        input_event events[32];
        ssize_t n;
        while ((n = ::read(it->second, events, sizeof events)) > 0) {
            for (size_t i = 0; i < n / sizeof(input_event); ++i) {
                const input_event &ev = events[i];
                if (ev.type == EV_KEY && ev.value != 2) {
                    out.push_back({ev.code, ev.value == 1});
                } else if (ev.type == EV_REL && ev.code == REL_WHEEL) {
                    for (int notch = 0; notch < std::abs(ev.value); ++notch)
                        out.push_back({ev.value > 0 ? keycode::WheelUp : keycode::WheelDown, true});
                }
            }
        }
        if (n < 0 && errno != EAGAIN && errno != EINTR) {
            // Device unplugged.
            close(it->second);
            it = fds_.erase(it);
        } else {
            ++it;
        }
    }
    return out;
}

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
            if (timeoutMs >= 0) {
                auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
                if (left <= 0) return false;
                wait = static_cast<int>(std::min<long long>(left, wait));
            }
            std::vector<pollfd> fds;
            input_.addPollFds(fds);
            int ready = poll(fds.data(), fds.size(), wait);
            if (ready == 0) input_.rescan();
            for (const InputEvent &e : input_.read()) pending_.push_back(e);
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
