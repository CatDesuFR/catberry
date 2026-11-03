#include "quickclick.h"

#include "daemon.h"
#include "macro.h"
#include "output.h"
#include "platform.h"
#include "shakecursor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <system_error>

namespace {

fs::path folder() {
    fs::path dir = configDir() / "quickclicks";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

fs::path setPath(const std::string &name) { return folder() / (name + ".conf"); }

} // namespace

const char *clickTypeName(ClickType type) {
    switch (type) {
    case ClickType::Right: return "right";
    case ClickType::Middle: return "middle";
    case ClickType::Double: return "double";
    default: return "left";
    }
}

bool parseClickType(const std::string &text, ClickType &type) {
    for (ClickType t : {ClickType::Left, ClickType::Right, ClickType::Middle, ClickType::Double})
        if (text == clickTypeName(t)) {
            type = t;
            return true;
        }
    return false;
}

bool writeQuickSet(const std::string &name, const QuickSet &set) {
    if (!validName(name)) return false;
    std::ofstream out(setPath(name));
    out << "desktop=" << set.desktopW << "x" << set.desktopH << "\n"
        << "hotkey=" << (set.hotkey >= 0 ? keyName(set.hotkey) : std::string()) << "\n"
        << "gap_us=" << std::clamp(set.gapUs, kMinGapUs, kMaxGapUs) << "\n";
    for (const QuickPoint &p : set.points) out << "point=" << p.x << "," << p.y << "," << clickTypeName(p.type) << "\n";
    return static_cast<bool>(out.flush());
}

bool readQuickSet(const std::string &name, QuickSet &set, std::string &error) {
    if (!validName(name)) {
        error = "invalid name '" + name + "'";
        return false;
    }
    std::ifstream in(setPath(name));
    if (!in) {
        error = "no quick click named '" + name + "'";
        return false;
    }
    QuickSet loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (line.empty() || eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "desktop") {
            std::sscanf(value.c_str(), "%dx%d", &loaded.desktopW, &loaded.desktopH);
        } else if (key == "hotkey") {
            loaded.hotkey = value.empty() ? -1 : parseKey(value);
        } else if (key == "gap_us") {
            loaded.gapUs = std::clamp(std::atoi(value.c_str()), kMinGapUs, kMaxGapUs);
        } else if (key == "point") {
            QuickPoint p{};
            char type[16] = "";
            ClickType t;
            if (std::sscanf(value.c_str(), "%d,%d,%15s", &p.x, &p.y, type) != 3 || !parseClickType(type, t)) {
                error = "'" + name + "' has a line that is not understood: " + line;
                return false;
            }
            p.type = t;
            loaded.points.push_back(p);
        }
    }
    set = loaded;
    return true;
}

std::vector<std::string> quickSetNames() {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(folder(), ec))
        if (entry.is_regular_file(ec) && entry.path().extension() == ".conf")
            names.push_back(entry.path().stem().string());
    std::sort(names.begin(), names.end());
    return names;
}

bool deleteQuickSet(const std::string &name) {
    std::error_code ec;
    return validName(name) && fs::remove(setPath(name), ec);
}

std::string quickPidName(const std::string &name) { return "quick." + name; }

QuickPoint scaledPoint(const QuickPoint &p, int fromW, int fromH, int toW, int toH) {
    if (fromW <= 0 || fromH <= 0 || (fromW == toW && fromH == toH)) return p;
    return {int(std::lround(double(p.x) * toW / fromW)), int(std::lround(double(p.y) * toH / fromH)), p.type};
}

void runQuickSet(const QuickSet &set, Output &output, KeyWatcher &keys) {
    auto gap = std::chrono::microseconds(set.gapUs);
    for (const QuickPoint &picked : set.points) {
        // The hotkey again stops the run (checked without waiting).
        InputEvent ev;
        while (keys.next(ev, 0))
            if (ev.down && ev.code == set.hotkey) return;
        const QuickPoint &p = picked;
        output.moveTo(p.x, p.y, set.desktopW, set.desktopH);
        int button = p.type == ClickType::Right    ? keycode::MouseRight
                     : p.type == ClickType::Middle ? keycode::MouseMiddle
                                                   : keycode::MouseLeft;
        for (int i = 0; i < (p.type == ClickType::Double ? 2 : 1); ++i) {
            output.pointerButton(button, true);
            output.pointerButton(button, false);
        }
        // Below 100 µs between points the desktop drops clicks.
        sleepUntil(Clock::now() + gap, [&] { return keys.stopRequested(); });
    }
}

int serveQuickSet(const std::string &name, const QuickSet &set,
                  const std::function<void(const std::string &error)> &ready) {
    std::string error;
    if (set.points.empty() || set.hotkey < 0) {
        ready("'" + name + "' has no points or no hotkey");
        return 1;
    }
    std::unique_ptr<KeyWatcher> keys = KeyWatcher::create(quickPidName(name), isMouseButton(set.hotkey), error);
    std::unique_ptr<Output> output = keys ? createOutput("", false, error, true) : nullptr;
    if (output && !output->canPoint()) {
        output.reset();
        error = "cannot move the pointer";
    }
    if (!output) {
        ready(error);
        return 1;
    }
    writeLoadedPid(quickPidName(name));
    ready("");

    ShakeCursorPause shake;
    // When to switch Shake Cursor back on: a while after the last run.
    Clock::time_point resumeAt{};
    while (!keys->stopRequested()) {
        int timeout = -1;
        if (shake.paused()) {
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(resumeAt - Clock::now()).count();
            if (left <= 0) {
                shake.resume();
                continue;
            }
            timeout = int(left);
        }
        InputEvent ev;
        if (!keys->next(ev, timeout) || !ev.down || ev.code != set.hotkey) continue;
        shake.pause();
        runQuickSet(set, *output, *keys);
        resumeAt = Clock::now() + std::chrono::milliseconds(300);
    }
    if (shake.paused()) shake.resume();
    if (loadedPid(quickPidName(name)) == currentPid()) forgetLoaded(quickPidName(name));
    return 0;
}
