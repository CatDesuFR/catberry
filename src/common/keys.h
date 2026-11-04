// Key codes and names shared by every platform.
#pragma once

#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

// Codes follow the Linux evdev numbering (KEY_*, BTN_*) on every platform,
// so configs and recorded macros can be moved between Linux and Windows.
namespace keycode {
constexpr int Escape = 1;
constexpr int F6 = 64;
constexpr int F8 = 66;
constexpr int F9 = 67;
constexpr int MouseLeft = 0x110;
constexpr int MouseRight = 0x111;
constexpr int MouseMiddle = 0x112;
constexpr int MouseBack = 0x113;
constexpr int MouseForward = 0x114;
// The wheel has no evdev key code (it is an axis there).
constexpr int WheelUp = 0x10000;
constexpr int WheelDown = 0x10001;
} // namespace keycode

inline bool isWheel(int code) { return code == keycode::WheelUp || code == keycode::WheelDown; }
inline bool isMouseButton(int code) { return code >= 0x110 && code < 0x120; }

// Names as used on the command line: "E", "F6", "LMB", "ScrollUp", ...
int parseKey(const std::string &name); // -1 if unknown
std::string keyName(int code);

// A press or release seen anywhere on the desktop. Wheel notches only come
// as presses.
struct InputEvent {
    int code;
    bool down;
};

// One key can come from several devices: a keyboard that shows up as two,
// a mouse or a macro pad with keys of its own. Merged, a key is down while
// any device holds it, so one press is one down and one up.
class PressMerger {
public:
    // `ev` from `device`: true when it should be passed on.
    bool feed(int device, const InputEvent &ev);
    // `device` went away: releases of the keys only it was holding.
    void forget(int device, std::vector<InputEvent> &released);

private:
    std::map<int, std::set<int>> held_; // key code -> devices holding it
};

// An event with when it happened (µs) and which device sent it.
struct TimedEvent {
    long long us;
    int device;
    InputEvent ev;
};

// Events read device by device, merged in the order they happened (so two
// devices reporting one press overlap, instead of looking like two presses).
std::vector<InputEvent> mergeInOrder(PressMerger &merger, std::vector<TimedEvent> events);
