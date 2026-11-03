// Output through uinput virtual devices (they reach every window, Wayland or X11).

#include "../common/output.h"
#include "linux.h"

#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

// The portable key codes are evdev codes; make sure they stay that way.
static_assert(keycode::Escape == KEY_ESC && keycode::F6 == KEY_F6 && keycode::F8 == KEY_F8 &&
                  keycode::F9 == KEY_F9,
              "key codes must match evdev");
static_assert(keycode::MouseLeft == BTN_LEFT && keycode::MouseRight == BTN_RIGHT &&
                  keycode::MouseMiddle == BTN_MIDDLE && keycode::MouseBack == BTN_SIDE &&
                  keycode::MouseForward == BTN_EXTRA,
              "mouse codes must match evdev");

const int kVendor = 0x1209;
const int kMouseProduct = 0xcab0;
const int kKeyboardProduct = 0xcab1;

const char *const kInputGroupCommand = "sudo usermod -aG input $USER";

bool isWayland() {
    const char *type = std::getenv("XDG_SESSION_TYPE");
    return std::getenv("WAYLAND_DISPLAY") || (type && std::strcmp(type, "wayland") == 0);
}

namespace {

class UinputOutput : public Output {
public:
    static std::unique_ptr<Output> create(bool withKeyboard) {
        // libinput debounces mouse buttons (a state change is held back for
        // ~25 ms), which swallows fast clicks. Its stock quirks switch that
        // off for emulated mice by matching exactly this device name
        // (10-generic-mouse.quirks), so borrow it rather than requiring a
        // root-owned quirks file.
        int mouse = createDevice("ImPS/2 Generic Wheel Mouse", kMouseProduct, true);
        if (mouse < 0) return nullptr;
        int keyboard = -1;
        if (withKeyboard) {
            keyboard = createDevice("catberry virtual keyboard", kKeyboardProduct, false);
            if (keyboard < 0) {
                destroyDevice(mouse);
                return nullptr;
            }
        }
        return std::unique_ptr<Output>(new UinputOutput(mouse, keyboard));
    }
    ~UinputOutput() override {
        destroyDevice(mouse_);
        if (keyboard_ >= 0) destroyDevice(keyboard_);
    }
    const char *name() const override { return "uinput"; }
    void key(int code, bool down) override {
        if (isWheel(code)) {
            if (!down) return;
            emit(mouse_, EV_REL, REL_WHEEL, code == keycode::WheelUp ? 1 : -1);
            emit(mouse_, EV_SYN, SYN_REPORT, 0);
            return;
        }
        int fd = isMouseButton(code) ? mouse_ : keyboard_;
        if (fd < 0) return;
        emit(fd, EV_KEY, code, down ? 1 : 0);
        emit(fd, EV_SYN, SYN_REPORT, 0);
    }

private:
    UinputOutput(int mouse, int keyboard) : mouse_(mouse), keyboard_(keyboard) {}

    static int createDevice(const char *name, int product, bool mouse) {
        int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) return -1;
        bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0;
        if (mouse) {
            for (int button : {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA})
                ok = ok && ioctl(fd, UI_SET_KEYBIT, button) == 0;
            // REL_X/REL_Y are never emitted, but without them the
            // compositor does not treat the device as a pointer.
            ok = ok && ioctl(fd, UI_SET_EVBIT, EV_REL) == 0 && ioctl(fd, UI_SET_RELBIT, REL_X) == 0 &&
                 ioctl(fd, UI_SET_RELBIT, REL_Y) == 0 && ioctl(fd, UI_SET_RELBIT, REL_WHEEL) == 0;
        } else {
            for (int key = KEY_ESC; key < 256; ++key) ok = ok && ioctl(fd, UI_SET_KEYBIT, key) == 0;
        }
        uinput_setup setup{};
        setup.id.bustype = BUS_VIRTUAL;
        setup.id.vendor = kVendor;
        setup.id.product = product;
        std::snprintf(setup.name, sizeof setup.name, "%s", name);
        ok = ok && ioctl(fd, UI_DEV_SETUP, &setup) == 0 && ioctl(fd, UI_DEV_CREATE) == 0;
        if (!ok) {
            close(fd);
            return -1;
        }
        return fd;
    }
    static void destroyDevice(int fd) {
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
    }
    static void emit(int fd, int type, int code, int value) {
        input_event ev{};
        ev.type = type;
        ev.code = code;
        ev.value = value;
        if (write(fd, &ev, sizeof ev) < 0) {
            // A dropped event only costs one click.
        }
    }

    int mouse_;
    int keyboard_;
};

} // namespace

std::unique_ptr<Output> createOutput(const std::string &backend, bool withKeyboard, std::string &error) {
    if (!backend.empty() && backend != "uinput") {
        error = "unknown backend '" + backend + "' (only uinput)";
        return nullptr;
    }
    std::unique_ptr<Output> out = UinputOutput::create(withKeyboard);
    if (!out) error = "cannot use /dev/uinput (the catberry launcher package adds the rule that allows it)";
    return out;
}
