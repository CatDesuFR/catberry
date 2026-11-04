// Linux-only pieces shared between the files in src/linux.
#pragma once

#include "../common/keys.h"

#include <poll.h>

#include <map>
#include <string>
#include <vector>

extern const char *const kInputGroupCommand;

// USB IDs of our virtual devices, so capture can skip our own output.
extern const int kVendor;
extern const int kMouseProduct;
extern const int kKeyboardProduct;
extern const int kPointerProduct;

bool isWayland();

// Reads every keyboard and mouse directly, which is the only way to see
// input globally on Wayland. Needs read access to /dev/input (input group).
class EvdevInput {
public:
    EvdevInput() { rescan(); }
    ~EvdevInput();
    EvdevInput(const EvdevInput &) = delete;
    EvdevInput &operator=(const EvdevInput &) = delete;

    bool available() const { return !fds_.empty(); }
    // Picks up devices plugged in since the last scan.
    void rescan();
    void addPollFds(std::vector<pollfd> &out) const;
    // Everything pending: presses, releases and wheel notches (no autorepeat).
    std::vector<InputEvent> read();

private:
    std::map<std::string, int> fds_;
    PressMerger merged_; // one press, even when several devices report it
};

// SIGTERM/SIGINT/SIGHUP make KeyWatcher::stopRequested() true.
void installStopSignals();
