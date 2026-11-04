#include "keys.h"

#include <cctype>
#include <cstdlib>

namespace {

// Linux evdev codes. The first name listed for a code is the one keyName()
// returns.
const struct {
    const char *name;
    int code;
} kKeys[] = {
    {"LMB", keycode::MouseLeft}, {"RMB", keycode::MouseRight}, {"MMB", keycode::MouseMiddle},
    {"MB4", keycode::MouseBack}, {"MB5", keycode::MouseForward},
    {"ScrollUp", keycode::WheelUp}, {"ScrollDown", keycode::WheelDown},
    {"A", 30}, {"B", 48}, {"C", 46}, {"D", 32}, {"E", 18}, {"F", 33}, {"G", 34}, {"H", 35}, {"I", 23},
    {"J", 36}, {"K", 37}, {"L", 38}, {"M", 50}, {"N", 49}, {"O", 24}, {"P", 25}, {"Q", 16}, {"R", 19},
    {"S", 31}, {"T", 20}, {"U", 22}, {"V", 47}, {"W", 17}, {"X", 45}, {"Y", 21}, {"Z", 44},
    {"1", 2}, {"2", 3}, {"3", 4}, {"4", 5}, {"5", 6}, {"6", 7}, {"7", 8}, {"8", 9}, {"9", 10}, {"0", 11},
    {"F1", 59}, {"F2", 60}, {"F3", 61}, {"F4", 62}, {"F5", 63}, {"F6", 64}, {"F7", 65}, {"F8", 66},
    {"F9", 67}, {"F10", 68}, {"F11", 87}, {"F12", 88},
    {"Space", 57}, {"Enter", 28}, {"Return", 28}, {"Tab", 15}, {"Esc", 1}, {"Escape", 1},
    {"Backspace", 14}, {"CapsLock", 58},
    {"Shift", 42}, {"LShift", 42}, {"RShift", 54}, {"Ctrl", 29}, {"LCtrl", 29}, {"RCtrl", 97},
    {"Alt", 56}, {"LAlt", 56}, {"RAlt", 100}, {"Super", 125}, {"Meta", 125}, {"Win", 125}, {"RSuper", 126},
    {"Up", 103}, {"Down", 108}, {"Left", 105}, {"Right", 106},
    {"Insert", 110}, {"Delete", 111}, {"Home", 102}, {"End", 107}, {"PageUp", 104}, {"PageDown", 109},
    {"PrintScreen", 99}, {"Pause", 119},
    {"Minus", 12}, {"Equal", 13}, {"LBracket", 26}, {"RBracket", 27}, {"Semicolon", 39},
    {"Apostrophe", 40}, {"Grave", 41}, {"Backslash", 43}, {"Comma", 51}, {"Period", 52}, {"Slash", 53},
    {"KP0", 82}, {"KP1", 79}, {"KP2", 80}, {"KP3", 81}, {"KP4", 75}, {"KP5", 76}, {"KP6", 77},
    {"KP7", 71}, {"KP8", 72}, {"KP9", 73}, {"KPEnter", 96}, {"KPPlus", 78}, {"KPMinus", 74},
    {"KPMultiply", 55}, {"KPDivide", 98}, {"KPPeriod", 83},
    {"NumLock", 69}, {"ScrollLock", 70}, {"Menu", 127},
};

bool equalsIgnoreCase(const char *a, const std::string &b) {
    size_t i = 0;
    for (; a[i] && i < b.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return a[i] == '\0' && i == b.size();
}

} // namespace

int parseKey(const std::string &name) {
    for (const auto &k : kKeys)
        if (equalsIgnoreCase(k.name, name)) return k.code;
    // Fallback spelling produced by keyName() for keys without a name.
    if (name.size() > 3 && equalsIgnoreCase("key", name.substr(0, 3))) {
        char *end = nullptr;
        long code = std::strtol(name.c_str() + 3, &end, 10);
        if (*end == '\0' && code > 0 && code < 256) return static_cast<int>(code);
    }
    return -1;
}

std::string keyName(int code) {
    for (const auto &k : kKeys)
        if (k.code == code) return k.name;
    return "key" + std::to_string(code);
}

bool PressMerger::feed(int device, const InputEvent &ev) {
    if (isWheel(ev.code)) return true; // notches, not presses
    std::set<int> &devices = held_[ev.code];
    if (ev.down) {
        bool first = devices.empty();
        devices.insert(device);
        return first;
    }
    if (!devices.erase(device)) return false; // it never pressed it
    return devices.empty();
}

void PressMerger::forget(int device, std::vector<InputEvent> &released) {
    for (auto &entry : held_)
        if (entry.second.erase(device) && entry.second.empty()) released.push_back({entry.first, false});
}
