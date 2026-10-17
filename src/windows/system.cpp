// Paths and background processes on Windows.

#include "../common/macro.h"
#include "../common/platform.h"
#include "win.h"

#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>
#include <fstream>
#include <sstream>
#include <system_error>

namespace {

fs::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    fs::path result = SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)) ? fs::path(path) : fs::path(L".");
    CoTaskMemFree(path);
    return result;
}

fs::path ensure(const fs::path &dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

// Signalled by -configunload to ask a loaded config to exit cleanly.
std::wstring stopEventName(const std::string &name) { return L"Local\\catberry-stop-" + widen(name); }

// Where a background process that failed to start leaves its reason.
fs::path errorPath(const std::string &name) { return runtimeDir() / (name + ".err"); }

class HookWatcher : public KeyWatcher {
public:
    HookWatcher(const std::string &name, bool mouse)
        : hooks_(mouse, nullptr, 0), stop_(CreateEventW(nullptr, TRUE, FALSE, stopEventName(name).c_str())) {}
    ~HookWatcher() override {
        if (stop_) CloseHandle(stop_);
    }

    bool ok() const { return hooks_.ok() && stop_; }

    bool next(InputEvent &ev, int timeoutMs) override {
        ULONGLONG deadline = GetTickCount64() + (timeoutMs < 0 ? 0 : timeoutMs);
        for (;;) {
            if (!pending_.empty()) {
                ev = pending_.front();
                pending_.pop_front();
                return true;
            }
            if (stopped_) return false;
            DWORD wait = INFINITE;
            if (timeoutMs >= 0) {
                ULONGLONG now = GetTickCount64();
                if (now >= deadline) return false;
                wait = static_cast<DWORD>(deadline - now);
            }
            // The hooks only run while this thread handles messages, so wait
            // for messages too, never just sleep.
            DWORD result = MsgWaitForMultipleObjects(1, &stop_, FALSE, wait, QS_ALLINPUT);
            if (result == WAIT_OBJECT_0) {
                stopped_ = true;
            } else if (result == WAIT_OBJECT_0 + 1) {
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) stopped_ = true;
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                for (const InputEvent &e : hooks_.take()) pending_.push_back(e);
            }
        }
    }

    bool stopRequested() const override { return stopped_; }

private:
    HookInput hooks_;
    HANDLE stop_;
    bool stopped_ = false;
    std::deque<InputEvent> pending_;
};

} // namespace

fs::path configDir() { return ensure(knownFolder(FOLDERID_RoamingAppData) / "catberry"); }
fs::path runtimeDir() { return ensure(knownFolder(FOLDERID_LocalAppData) / "catberry" / "run"); }

void sleepUntil(Clock::time_point deadline, const std::function<bool()> &cancelled) {
    // A high-resolution waitable timer (Windows 10 1803+) wakes within about
    // half a millisecond; older systems fall back to a normal timer.
    thread_local HANDLE timer = [] {
        HANDLE t = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
                                          TIMER_ALL_ACCESS);
        return t ? t : CreateWaitableTimerW(nullptr, TRUE, nullptr);
    }();
    // Short slices so a cancel is noticed quickly.
    while (!cancelled()) {
        auto left = deadline - Clock::now();
        if (left <= Clock::duration::zero()) return;
        auto slice = std::min<Clock::duration>(left, std::chrono::milliseconds(10));
        LARGE_INTEGER due;
        // Negative: relative, in 100 ns units.
        due.QuadPart = -std::max<long long>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(slice).count() / 100);
        if (!timer || !SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
            Sleep(1);
            continue;
        }
        WaitForSingleObject(timer, INFINITE);
    }
}

long currentPid() { return static_cast<long>(GetCurrentProcessId()); }

bool isCatberryProcess(long pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) return false;
    DWORD exitCode = 0;
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    bool alive = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
    bool named = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    if (!alive || !named) return false;
    std::wstring file = fs::path(path).filename().wstring();
    std::transform(file.begin(), file.end(), file.begin(), ::towlower);
    return file == L"catberry.exe";
}

void stopProcess(long pid, const std::string &name) {
    if (HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE, stopEventName(name).c_str())) {
        SetEvent(stop);
        CloseHandle(stop);
    }
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (!process) return;
    if (WaitForSingleObject(process, 2000) != WAIT_OBJECT_0) TerminateProcess(process, 1);
    CloseHandle(process);
}

bool launchDaemon(const std::string &name, const MacroConfig &, std::string &error) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(exe) + L"\" --serve " + widen(name);
    std::error_code ec;
    fs::remove(errorPath(name), ec);

    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION info{};
    // Detached: no console window, and it outlives the terminal.
    if (!CreateProcessW(exe, &command[0], nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                        nullptr, nullptr, &startup, &info)) {
        error = "cannot start the background process";
        return false;
    }
    CloseHandle(info.hThread);
    bool started = false;
    for (int i = 0; i < 100 && !started; ++i) {
        if (WaitForSingleObject(info.hProcess, 50) == WAIT_OBJECT_0) break;
        started = loadedPid(name) == static_cast<long>(info.dwProcessId);
    }
    if (!started) {
        std::ifstream in(errorPath(name));
        std::stringstream reason;
        reason << in.rdbuf();
        error = reason.str().empty() ? "the background process failed to start" : reason.str();
        in.close();
        fs::remove(errorPath(name), ec);
        TerminateProcess(info.hProcess, 1);
    }
    CloseHandle(info.hProcess);
    return started;
}

int serveMain(int argc, char **argv) {
    if (argc != 1 || !validName(argv[0])) return 2;
    std::string name = argv[0], error;
    MacroConfig cfg;
    FineTimer timer;
    if (!readConfig(name, cfg, error)) {
        std::ofstream(errorPath(name)) << error;
        return 1;
    }
    return serveConfig(name, cfg, [&](const std::string &err) {
        if (!err.empty()) std::ofstream(errorPath(name)) << err;
    });
}

std::unique_ptr<KeyWatcher> KeyWatcher::create(const std::string &name, bool mouse, std::string &error) {
    std::unique_ptr<HookWatcher> watcher(new HookWatcher(name, mouse));
    if (!watcher->ok()) {
        error = "cannot watch the keyboard for the hotkey";
        return nullptr;
    }
    return watcher;
}
