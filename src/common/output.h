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

    // Absolute pointer moves (quick click): only when created with withPointer.
    virtual bool canPoint() const { return false; }
    // Jumps the pointer to (x, y) on a desktop of desktopW x desktopH (all
    // screens together, 0,0 at the top left).
    virtual void moveTo(int x, int y, int desktopW, int desktopH) {
        (void)x, (void)y, (void)desktopW, (void)desktopH;
    }
    // A mouse button at the pointer, in order with moveTo.
    virtual void pointerButton(int code, bool down) { key(code, down); }
};

// Implemented per platform. `backend` forces one ("" picks automatically);
// on failure returns null and sets `error`.
// withPointer also makes the absolute pointer of quick click.
std::unique_ptr<Output> createOutput(const std::string &backend, bool withKeyboard, std::string &error,
                                     bool withPointer = false);
