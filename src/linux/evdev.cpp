// Global input through /dev/input.

#include "linux.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

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
                      !(id.vendor == kVendor && (id.product == kMouseProduct || id.product == kKeyboardProduct ||
                                                    id.product == kPointerProduct));
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
    // Everything waiting on every device first, then merged in time order.
    std::vector<TimedEvent> waiting;
    std::vector<InputEvent> out;
    for (auto it = fds_.begin(); it != fds_.end();) {
        input_event events[32];
        ssize_t n;
        while ((n = ::read(it->second, events, sizeof events)) > 0) {
            for (size_t i = 0; i < n / sizeof(input_event); ++i) {
                const input_event &ev = events[i];
                long long us = (long long)ev.input_event_sec * 1000000 + ev.input_event_usec;
                if (ev.type == EV_KEY && ev.value != 2) {
                    waiting.push_back({us, it->second, {ev.code, ev.value == 1}});
                } else if (ev.type == EV_REL && ev.code == REL_WHEEL) {
                    for (int notch = 0; notch < std::abs(ev.value); ++notch)
                        waiting.push_back({us, it->second, {ev.value > 0 ? keycode::WheelUp : keycode::WheelDown, true}});
                }
            }
        }
        if (n < 0 && errno != EAGAIN && errno != EINTR) {
            // Device unplugged: release what only it was holding.
            merged_.forget(it->second, out);
            close(it->second);
            it = fds_.erase(it);
        } else {
            ++it;
        }
    }
    std::vector<InputEvent> merged = mergeInOrder(merged_, waiting);
    // Releases of unplugged devices' keys (in `out`) come after.
    merged.insert(merged.end(), out.begin(), out.end());
    return merged;
}
