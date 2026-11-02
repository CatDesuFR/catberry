// The windows' logic is shared; each platform supplies the window, drawing
// and global input through Host and Canvas.
#pragma once

#include "keys.h"
#include "output.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

enum class Color { Bg, Panel, Border, Text, Dim, Accent, Go, Stop };

// A key typed while one of our windows has focus. A platform may report the
// key and the character it types as two separate presses.
struct TypedKey {
    enum Special { None, Enter, Escape, Backspace, Tab };
    int code = -1; // key code, or -1 for a character-only press
    char ch = 0;   // printable ASCII character, or 0
    Special special = None;
    bool modifier = false; // Shift, Ctrl, Alt, Super, CapsLock or NumLock
};

// Coordinates are in unscaled pixels; text y is the baseline.
class Canvas {
public:
    virtual ~Canvas() = default;
    virtual void clear() = 0;
    virtual void text(int x, int y, const std::string &s, Color color) = 0;
    // Text vertically centred on `row`.
    virtual void label(int x, const Rect &row, const std::string &s, Color color) = 0;
    virtual void button(const Rect &r, const std::string &s, Color bg, Color fg) = 0;
};

enum class HotkeyScope { Global, X11WindowsOnly, Taken };

class Host {
public:
    virtual ~Host() = default;
    virtual Output &output() = 0;
    // True when output only reaches some windows (XTest on Wayland).
    virtual bool outputLimited() const = 0;
    virtual HotkeyScope hotkeyScope() const = 0;
    virtual bool canRecord() const = 0;
    // Command that would lift the limits above, or null.
    virtual const char *permissionFix() const = 0;
    // Runs `fn` on the window's thread; callable from any thread.
    virtual void post(std::function<void()> fn) = 0;
};

class App {
public:
    virtual ~App() = default;
    virtual const char *title() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual void draw(Canvas &canvas) = 0;
    // button: 1 left, 2 middle, 3 right, 4/5 wheel up/down.
    virtual void click(int x, int y, int button) = 0;
    virtual void key(const TypedKey &press) = 0;
    virtual void focusLost() = 0;
    // Key and mouse-button transitions anywhere on the desktop.
    virtual void globalInput(const InputEvent &ev) = 0;
    // Called about every 100 ms while wantsTicks() is true.
    virtual void tick() {}
    virtual bool wantsTicks() const { return false; }
    // Keys a host has to grab when it cannot see all input.
    virtual std::vector<int> watchedKeys() const = 0;

    bool dirty = true; // the host redraws when set
};

struct GuiOptions {
    std::string backend;         // forced output backend, "" for automatic
    bool keyboardOutput = false; // sends key presses, not only clicks
    bool recordsMouse = false;   // needs global mouse buttons, not only keys
};

using AppFactory = std::function<std::unique_ptr<App>(Host &)>;
// Implemented per platform: opens the window and runs until it is closed.
int runGui(const AppFactory &make, const GuiOptions &options);

// Single-line input; the owning app draws it and routes key presses to it.
struct TextField {
    enum Kind { Decimal, Integer, Name };
    enum Result { Ignored, Changed, Committed };

    Rect rect;
    Kind kind;
    size_t maxLen;
    std::string text;
    bool focused = false;

    Result key(const TypedKey &press);
    void draw(Canvas &canvas) const;
};
