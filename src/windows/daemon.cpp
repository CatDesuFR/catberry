// Background processes for loaded configs on Windows: a detached copy of
// the program (`--serve NAME`) that watches the keyboard with hooks.

#include "../common/daemon.h"
#include "../common/macro.h"
#include "../common/quickclick.h"
#include "win.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>
#include <fstream>
#include <sstream>
#include <system_error>

namespace {

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
            bool lastLook = false;
            if (timeoutMs >= 0) {
                // Out of time (or timeout 0): handle what is waiting once,
                // so the hooks run and a pressed hotkey is seen, then stop.
                ULONGLONG now = GetTickCount64();
                lastLook = now >= deadline;
                wait = lastLook ? 0 : static_cast<DWORD>(deadline - now);
            }
            // The hooks only run while this thread handles messages, so wait
            // for messages too, never just sleep. On the last look, empty the
            // queue directly: MsgWait only reports messages that are new.
            DWORD result = lastLook ? (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0 ? WAIT_OBJECT_0 : WAIT_OBJECT_0 + 1)
                                    : MsgWaitForMultipleObjects(1, &stop_, FALSE, wait, QS_ALLINPUT);
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
            if (lastLook && pending_.empty()) return false;
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
    return file == L"catberry-mtools.exe";
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

namespace {

// Starts `<this exe> <flag> <name>` detached; it writes `pidName`'s pid
// file once it runs, or its error to `pidName`.err.
bool launchServe(const wchar_t *flag, const std::string &name, const std::string &pidName, std::string &error) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(exe) + L"\" " + flag + L" " + widen(name);
    std::error_code ec;
    fs::remove(errorPath(pidName), ec);

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
        started = loadedPid(pidName) == static_cast<long>(info.dwProcessId);
    }
    if (!started) {
        std::ifstream in(errorPath(pidName));
        std::stringstream reason;
        reason << in.rdbuf();
        error = reason.str().empty() ? "the background process failed to start" : reason.str();
        in.close();
        fs::remove(errorPath(pidName), ec);
        TerminateProcess(info.hProcess, 1);
    }
    CloseHandle(info.hProcess);
    return started;
}

} // namespace

bool launchDaemon(const std::string &name, const MacroConfig &, std::string &error) {
    return launchServe(L"--serve", name, name, error);
}

bool launchQuickDaemon(const std::string &name, const QuickSet &, std::string &error) {
    return launchServe(L"--serve-quick", name, quickPidName(name), error);
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

int serveQuickMain(int argc, char **argv) {
    if (argc != 1 || !validName(argv[0])) return 2;
    std::string name = argv[0], error, pidName = quickPidName(argv[0]);
    QuickSet set;
    FineTimer timer;
    if (!readQuickSet(name, set, error)) {
        std::ofstream(errorPath(pidName)) << error;
        return 1;
    }
    return serveQuickSet(name, set, [&](const std::string &err) {
        if (!err.empty()) std::ofstream(errorPath(pidName)) << err;
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
