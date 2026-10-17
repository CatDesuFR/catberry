// Windows-only pieces shared between the files in src/windows.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "../common/keys.h"

#include <set>
#include <string>
#include <vector>

std::wstring widen(const std::string &s);

// Key code for a key as Windows reports it, or -1 for keys we do not name.
int codeFromWindowsKey(unsigned vk, unsigned scan, bool extended);

// How to send a key code. Most keys go by scan code, which games read;
// a few odd ones only work by virtual-key code (vk != 0).
struct WinKey {
    unsigned scan;
    bool extended;
    unsigned vk;
};
bool windowsKeyFromCode(int code, WinKey &out);

// Low-level keyboard (and optionally mouse) hooks. The hooks run on the
// creating thread, which must keep pumping messages. One instance at a time.
class HookInput {
public:
    // When `notify` is set, `message` is posted to it whenever events arrive.
    HookInput(bool mouse, HWND notify, UINT message);
    ~HookInput();
    HookInput(const HookInput &) = delete;
    HookInput &operator=(const HookInput &) = delete;

    bool ok() const { return keyboard_ != nullptr; }
    std::vector<InputEvent> take();

private:
    static LRESULT CALLBACK keyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK mouseProc(int nCode, WPARAM wParam, LPARAM lParam);
    void push(int code, bool down);

    HHOOK keyboard_ = nullptr;
    HHOOK mouse_ = nullptr;
    HWND notify_;
    UINT message_;
    std::vector<InputEvent> queue_;
    std::set<int> down_;
};

// Raises the system timer resolution to 1 ms while alive. The default
// 15.6 ms would cap the clicker at about 64 clicks per second.
class FineTimer {
public:
    FineTimer();
    ~FineTimer();
};
