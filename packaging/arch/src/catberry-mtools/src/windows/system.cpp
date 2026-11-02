// Paths and timing on Windows.

#include "../common/platform.h"
#include "win.h"

#include <knownfolders.h>
#include <shlobj.h>

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
