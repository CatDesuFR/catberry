// Synthetic key presses and clicks.
#pragma once

#include <memory>
#include <string>

class Output {
public:
    virtual ~Output() = default;
    virtual const char *name() const = 0;
    // A wheel code scrolls one notch on `down` and ignores the release.
    virtual void key(int code, bool down) = 0;
};

// Implemented per platform. `backend` forces one ("" picks automatically);
// on failure returns null and sets `error`.
std::unique_ptr<Output> createOutput(const std::string &backend, bool withKeyboard, std::string &error);
