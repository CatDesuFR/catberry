#include "macro.h"

#include "output.h"
#include "daemon.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace {

fs::path subdir(const char *name) {
    fs::path dir = configDir() / name;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

fs::path macroPath(const std::string &name) { return subdir("macros") / (name + ".macro"); }
fs::path configPath(const std::string &name) { return subdir("configs") / (name + ".conf"); }
fs::path pidPath(const std::string &name) { return runtimeDir() / (name + ".pid"); }

// The names of the files in `dir` ending in `ext`, sorted.
std::vector<std::string> namesIn(const fs::path &dir, const char *ext) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(dir, ec))
        if (entry.is_regular_file(ec) && entry.path().extension() == ext) names.push_back(entry.path().stem().string());
    std::sort(names.begin(), names.end());
    return names;
}

// Watches for the hotkey and plays the key list when it goes down.
void serveMacro(const MacroConfig &cfg, KeyWatcher &keys, Output &output) {
    // Waits `ms`; true means abandon the macro (hotkey again, or stopping).
    auto interrupted = [&](int ms) {
        auto deadline = Clock::now() + std::chrono::milliseconds(ms);
        for (;;) {
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
            if (left <= 0) return keys.stopRequested();
            InputEvent ev;
            if (!keys.next(ev, static_cast<int>(left))) {
                if (keys.stopRequested()) return true;
                continue;
            }
            if (ev.down && ev.code == cfg.hotkey) return true;
        }
    };

    while (!keys.stopRequested()) {
        InputEvent ev;
        if (!keys.next(ev, -1) || !ev.down || ev.code != cfg.hotkey) continue;
        bool abort = false;
        for (int run = 0; run < cfg.count && !abort; ++run) {
            for (int code : cfg.keys) {
                output.key(code, true);
                abort = interrupted(cfg.delayMs);
                output.key(code, false);
                if (abort || (abort = interrupted(cfg.delayMs))) break;
            }
        }
    }
}

} // namespace

std::vector<std::string> macroNames() { return namesIn(subdir("macros"), ".macro"); }

bool deleteMacro(const std::string &name) {
    std::error_code ec;
    return validName(name) && fs::remove(macroPath(name), ec);
}

bool renameMacro(const std::string &from, const std::string &to) {
    std::error_code ec;
    if (!validName(from) || !validName(to) || !fs::exists(macroPath(from), ec) || fs::exists(macroPath(to), ec))
        return false;
    fs::rename(macroPath(from), macroPath(to), ec);
    return !ec;
}

std::vector<std::string> configNames() { return namesIn(subdir("configs"), ".conf"); }

bool deleteConfig(const std::string &name) {
    std::error_code ec;
    return validName(name) && fs::remove(configPath(name), ec);
}

bool saveMacro(const std::string &name, const Macro &macro) {
    std::ofstream out(macroPath(name));
    out << "catberry-macro 1\n" << "duration " << macro.durationUs << "\n";
    for (const MacroEvent &ev : macro.events) out << ev.us << " " << keyName(ev.code) << " " << (ev.down ? 1 : 0) << "\n";
    return static_cast<bool>(out.flush());
}

bool loadMacro(const std::string &name, Macro &macro) {
    std::ifstream in(macroPath(name));
    std::string magic, word;
    int version = 0;
    Macro loaded;
    if (!(in >> magic >> version) || magic != "catberry-macro" || version != 1) return false;
    if (!(in >> word >> loaded.durationUs) || word != "duration") return false;
    long long us;
    int down;
    while (in >> us >> word >> down) {
        int code = parseKey(word);
        if (code < 0) return false;
        loaded.events.push_back({us, code, down != 0});
    }
    macro = loaded;
    return true;
}

bool validName(const std::string &name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return false;
    return true;
}

bool parseKeyList(const std::string &text, std::vector<int> &keys, std::string &bad) {
    std::stringstream in(text);
    std::string item;
    while (std::getline(in, item, ',')) {
        int code = parseKey(item);
        if (code < 0) {
            bad = item;
            return false;
        }
        keys.push_back(code);
    }
    if (keys.empty()) bad = text;
    return !keys.empty();
}

bool readConfig(const std::string &name, MacroConfig &cfg, std::string &error) {
    std::ifstream in(configPath(name));
    if (!in) {
        error = "no config named '" + name + "' (create it with catberry -config)";
        return false;
    }
    std::string line, bad;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back(); // edited on Windows
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "keys") {
            cfg.keysText = value;
            cfg.keys.clear();
            if (!parseKeyList(value, cfg.keys, bad)) {
                error = "config '" + name + "' has an unknown key '" + bad + "'";
                return false;
            }
        } else if (key == "hotkey") {
            cfg.hotkey = parseKey(value);
        } else if (key == "count") {
            cfg.count = std::atoi(value.c_str());
        } else if (key == "delay_ms") {
            cfg.delayMs = std::atoi(value.c_str());
        }
    }
    if (cfg.keys.empty() || cfg.hotkey < 0 || cfg.count < 1 || cfg.delayMs < 0) {
        error = "config '" + name + "' is incomplete or invalid";
        return false;
    }
    return true;
}

bool writeConfig(const std::string &name, const MacroConfig &cfg) {
    std::ofstream out(configPath(name));
    out << "keys=" << cfg.keysText << "\n"
        << "hotkey=" << keyName(cfg.hotkey) << "\n"
        << "count=" << cfg.count << "\n"
        << "delay_ms=" << cfg.delayMs << "\n";
    return static_cast<bool>(out.flush());
}

long loadedPid(const std::string &name) {
    std::ifstream in(pidPath(name));
    long pid = 0;
    if (!(in >> pid) || pid <= 0) return 0;
    in.close();
    // Guards against the pid having been reused by an unrelated program.
    if (isCatberryProcess(pid)) return pid;
    std::error_code ec;
    fs::remove(pidPath(name), ec);
    return 0;
}

std::vector<std::string> loadedNames() {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(runtimeDir(), ec))
        if (entry.path().extension() == ".pid") names.push_back(entry.path().stem().string());
    return names;
}

void forgetLoaded(const std::string &name) {
    std::error_code ec;
    fs::remove(pidPath(name), ec);
}

int serveConfig(const std::string &name, const MacroConfig &cfg,
                const std::function<void(const std::string &error)> &ready) {
    std::string error;
    std::unique_ptr<KeyWatcher> keys = KeyWatcher::create(name, isMouseButton(cfg.hotkey), error);
    std::unique_ptr<Output> output = keys ? createOutput("", true, error) : nullptr;
    if (!output) {
        ready(error);
        return 1;
    }
    {
        std::ofstream pidFile(pidPath(name));
        pidFile << currentPid() << "\n";
    }
    ready("");
    serveMacro(cfg, *keys, *output);
    // A newer instance may own the pid file by now.
    if (loadedPid(name) == currentPid()) forgetLoaded(name);
    return 0;
}
