// Key code mapping, SendInput output and low-level hooks.

#include "../common/output.h"
#include "win.h"

#include <mmsystem.h>

#include <cstdlib>

namespace {

// Key codes are evdev codes, which for 1-88 equal the PC "set 1" scan codes
// Windows uses. Keys with an 0xE0 prefix (extended) are listed here.
const struct {
    int code;
    unsigned scan;
} kExtended[] = {
    {96, 0x1C},  // KPEnter
    {97, 0x1D},  // RCtrl
    {98, 0x35},  // KPDivide
    {100, 0x38}, // RAlt
    {102, 0x47}, // Home
    {103, 0x48}, // Up
    {104, 0x49}, // PageUp
    {105, 0x4B}, // Left
    {106, 0x4D}, // Right
    {107, 0x4F}, // End
    {108, 0x50}, // Down
    {109, 0x51}, // PageDown
    {110, 0x52}, // Insert
    {111, 0x53}, // Delete
    {125, 0x5B}, // Super
    {126, 0x5C}, // RSuper
    {127, 0x5D}, // Menu
};

// Keys whose scan codes are ambiguous; they go by virtual-key code.
const struct {
    int code;
    unsigned vk;
    bool extended;
} kByVk[] = {
    {69, VK_NUMLOCK, true},
    {99, VK_SNAPSHOT, false}, // PrintScreen
    {119, VK_PAUSE, false},
};

} // namespace

std::wstring widen(const std::string &s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

int codeFromWindowsKey(unsigned vk, unsigned scan, bool extended) {
    for (const auto &k : kByVk)
        if (k.vk == vk) return k.code;
    if (extended) {
        for (const auto &k : kExtended)
            if (k.scan == scan) return k.code;
        return -1;
    }
    return scan >= 1 && scan <= 88 ? static_cast<int>(scan) : -1;
}

bool windowsKeyFromCode(int code, WinKey &out) {
    for (const auto &k : kByVk) {
        if (k.code == code) {
            out = {0, k.extended, k.vk};
            return true;
        }
    }
    for (const auto &k : kExtended) {
        if (k.code == code) {
            out = {k.scan, true, 0};
            return true;
        }
    }
    if (code < 1 || code > 88) return false;
    out = {static_cast<unsigned>(code), false, 0};
    return true;
}

// ---------------------------------------------------------------------------

namespace {

class SendInputOutput : public Output {
public:
    const char *name() const override { return "SendInput"; }
    void key(int code, bool down) override {
        INPUT in{};
        if (isWheel(code)) {
            if (!down) return;
            in.type = INPUT_MOUSE;
            in.mi.dwFlags = MOUSEEVENTF_WHEEL;
            in.mi.mouseData = static_cast<DWORD>(code == keycode::WheelUp ? WHEEL_DELTA : -WHEEL_DELTA);
        } else if (isMouseButton(code)) {
            in.type = INPUT_MOUSE;
            switch (code) {
            case keycode::MouseLeft: in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
            case keycode::MouseRight: in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
            case keycode::MouseMiddle: in.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
            case keycode::MouseBack:
            case keycode::MouseForward:
                in.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
                in.mi.mouseData = code == keycode::MouseBack ? XBUTTON1 : XBUTTON2;
                break;
            default: return;
            }
        } else {
            WinKey k;
            if (!windowsKeyFromCode(code, k)) return;
            in.type = INPUT_KEYBOARD;
            DWORD flags = (down ? 0 : KEYEVENTF_KEYUP) | (k.extended ? KEYEVENTF_EXTENDEDKEY : 0);
            if (k.vk) {
                in.ki.wVk = static_cast<WORD>(k.vk);
            } else {
                in.ki.wScan = static_cast<WORD>(k.scan);
                flags |= KEYEVENTF_SCANCODE;
            }
            in.ki.dwFlags = flags;
        }
        SendInput(1, &in, sizeof in);
    }

    bool canPoint() const override { return true; }
    // 0-65535 over the whole virtual screen (all monitors).
    void moveTo(int x, int y, int desktopW, int desktopH) override {
        if (desktopW < 2 || desktopH < 2) return;
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dx = static_cast<LONG>((long long)x * 65535 / (desktopW - 1));
        in.mi.dy = static_cast<LONG>((long long)y * 65535 / (desktopH - 1));
        in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        SendInput(1, &in, sizeof in);
    }
};

HookInput *g_hooks = nullptr;

} // namespace

std::unique_ptr<Output> createOutput(const std::string &backend, bool, std::string &error, bool) {
    if (!backend.empty()) {
        error = "--backend is not available on Windows (it always uses SendInput)";
        return nullptr;
    }
    return std::unique_ptr<Output>(new SendInputOutput);
}

// ---------------------------------------------------------------------------

HookInput::HookInput(bool mouse, HWND notify, UINT message) : notify_(notify), message_(message) {
    g_hooks = this;
    HINSTANCE self = GetModuleHandleW(nullptr);
    keyboard_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, self, 0);
    if (mouse) mouse_ = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, self, 0);
}

HookInput::~HookInput() {
    if (keyboard_) UnhookWindowsHookEx(keyboard_);
    if (mouse_) UnhookWindowsHookEx(mouse_);
    g_hooks = nullptr;
}

std::vector<InputEvent> HookInput::take() {
    std::vector<InputEvent> out;
    out.swap(queue_);
    return out;
}

void HookInput::push(int code, bool down) {
    // Windows repeats key-down while a key is held; report it only once.
    if (!isWheel(code)) {
        if (down && !down_.insert(code).second) return;
        if (!down) down_.erase(code);
    }
    bool wasEmpty = queue_.empty();
    queue_.push_back({code, down});
    if (notify_ && wasEmpty) PostMessageW(notify_, message_, 0, 0);
}

LRESULT CALLBACK HookInput::keyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_hooks) {
        const auto *k = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
        // Injected events include our own output; a macro that sends its
        // own hotkey would otherwise retrigger itself.
        if (!(k->flags & LLKHF_INJECTED)) {
            int code = codeFromWindowsKey(k->vkCode, k->scanCode, (k->flags & LLKHF_EXTENDED) != 0);
            bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
            if (code >= 0) g_hooks->push(code, down);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK HookInput::mouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_hooks) {
        const auto *m = reinterpret_cast<const MSLLHOOKSTRUCT *>(lParam);
        if (!(m->flags & LLMHF_INJECTED)) {
            int xbutton = HIWORD(m->mouseData) == XBUTTON1 ? keycode::MouseBack : keycode::MouseForward;
            switch (wParam) {
            case WM_LBUTTONDOWN: g_hooks->push(keycode::MouseLeft, true); break;
            case WM_LBUTTONUP: g_hooks->push(keycode::MouseLeft, false); break;
            case WM_RBUTTONDOWN: g_hooks->push(keycode::MouseRight, true); break;
            case WM_RBUTTONUP: g_hooks->push(keycode::MouseRight, false); break;
            case WM_MBUTTONDOWN: g_hooks->push(keycode::MouseMiddle, true); break;
            case WM_MBUTTONUP: g_hooks->push(keycode::MouseMiddle, false); break;
            case WM_XBUTTONDOWN: g_hooks->push(xbutton, true); break;
            case WM_XBUTTONUP: g_hooks->push(xbutton, false); break;
            case WM_MOUSEWHEEL: {
                int delta = static_cast<short>(HIWORD(m->mouseData));
                int notches = std::abs(delta) / WHEEL_DELTA;
                for (int i = 0; i < (notches ? notches : 1); ++i)
                    g_hooks->push(delta > 0 ? keycode::WheelUp : keycode::WheelDown, true);
                break;
            }
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

FineTimer::FineTimer() { timeBeginPeriod(1); }
FineTimer::~FineTimer() { timeEndPeriod(1); }
