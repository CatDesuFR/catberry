// catberry-vskm: Vencord plugins for Vesktop, switched on and off from the
// command line.
//
//   catberry vskm -list            list the plugins and which are loaded
//   catberry vskm load NAME        switch a plugin on
//   catberry vskm unload NAME      switch it off
//   catberry vskm unload all       switch every vskm plugin off
//   catberry vskm -v               print the version
//
// Vencord can't load plugins at runtime, so vskm comes with its own Vencord
// build that has the plugins built in (installed in /usr/share/catberry-vskm/
// vencord). Loading a plugin points Vesktop at that build and switches the
// plugin on in Vencord's settings; once no vskm plugin is on, Vesktop goes
// back to its own Vencord. Vencord rewrites its settings file while Vesktop
// runs, so a running Vesktop is closed first and started again after.

#include "../third_party/json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern char **environ;

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

#ifndef VSKM_VERSION
#define VSKM_VERSION "0.0.0"
#endif

namespace {

struct Plugin {
    const char *name;     // what you type: MF, TS, ACA, OD
    const char *vencord;  // its name in Vencord
    const char *title;
    const char *desc;
};

const Plugin kPlugins[] = {
    {"MF", "VskmMiscFixes", "Misc Fixes",
     "Screen share sound on Linux works (Vesktop's audio share set up for you)."},
    {"TS", "VskmThemeStealer", "Theme Stealer",
     "Copies Discord's colour theme or a server's look into a theme you name and keep."},
    {"ACA", "VskmAllCharacters", "All Characters In Channel/Category Names",
     "Capital letters and symbols in channel names (look-alike characters; spaces show as dots)."},
    {"OD", "OptiDiscord", "OptiDiscord",
     "Makes Discord lighter and snappier: less memory, no work in the background, lighter media (choose in its settings)."},
};

const Plugin *findPlugin(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::toupper(c); });
    for (const Plugin &p : kPlugins)
        if (name == p.name) return &p;
    return nullptr;
}

std::string env(const char *name) {
    const char *v = std::getenv(name);
    return v ? v : "";
}

fs::path configHome() {
    std::string xdg = env("XDG_CONFIG_HOME");
    return xdg.empty() ? fs::path(env("HOME")) / ".config" : fs::path(xdg);
}

// Vesktop keeps its custom Vencord folder ("vencordDir") in state.json.
fs::path vesktopState() { return configHome() / "vesktop" / "state.json"; }
fs::path vencordSettings() { return configHome() / "vesktop" / "settings" / "settings.json"; }
fs::path vskmState() { return configHome() / "catberry" / "vskm.json"; }
fs::path vesktopSettings() { return configHome() / "vesktop" / "settings.json"; }
// Read by vskm's Vencord in a running Vesktop: plugins to stop (see
// plugins/vskmMiscFixes/native.ts).
fs::path stopRequest() { return configHome() / "catberry" / "vskm-stop"; }

// vskm's Vencord build: next to the program while developing, else installed.
fs::path vencordDir() {
    std::string over = env("CATBERRY_VSKM_VENCORD");
    if (!over.empty()) return over;
    std::error_code ec;
    fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        fs::path dev = self.parent_path() / "vencord";
        if (fs::exists(dev / "vencordDesktopMain.js", ec)) return dev;
    }
    return "/usr/share/catberry-vskm/vencord";
}

bool readJson(const fs::path &file, json &out) {
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        out = json::object();
        return true;
    }
    std::ifstream in(file);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    out = json::parse(text, nullptr, false);
    if (out.is_discarded() || !out.is_object()) {
        std::fprintf(stderr, "catberry vskm: %s isn't valid JSON, so it's left alone\n", file.c_str());
        return false;
    }
    return true;
}

bool writeJson(const fs::path &file, const json &value) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    fs::path tmp = file;
    tmp += ".vskm-tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << value.dump(4) << "\n";
        if (!out) return false;
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        std::fprintf(stderr, "catberry vskm: could not write %s: %s\n", file.c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

bool pluginOn(const json &vencord, const Plugin &p) {
    auto plugins = vencord.find("plugins");
    if (plugins == vencord.end() || !plugins->is_object()) return false;
    auto it = plugins->find(p.vencord);
    return it != plugins->end() && it->is_object() && it->value("enabled", false);
}

// The Vesktop using this settings folder, or 0. Electron keeps a lock in
// the folder that names its main process ("host-PID"); that works whatever
// the program is called (vesktop, or electron running Vesktop's app).
pid_t vesktopPid() {
    std::error_code ec;
    fs::path lock = configHome() / "vesktop" / "SingletonLock";
    std::string target = fs::read_symlink(lock, ec).string();
    if (ec) return 0;
    size_t dash = target.rfind('-');
    if (dash == std::string::npos) return 0;
    pid_t pid = static_cast<pid_t>(std::atol(target.c_str() + dash + 1));
    if (pid <= 0 || kill(pid, 0) != 0) return 0;  // stale lock
    struct stat st {};
    std::string proc = "/proc/" + std::to_string(pid);
    if (stat(proc.c_str(), &st) != 0 || st.st_uid != getuid()) return 0;
    return pid;
}

// How the closed Vesktop was started, to start it the same way again.
std::vector<std::string> g_vesktopCommand;

// The command line of a process, if every file it names still exists (an
// updated Vesktop may have replaced them; then "vesktop" is started).
std::vector<std::string> reusableCommand(pid_t pid) {
    std::ifstream in("/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary);
    std::vector<std::string> args;
    std::string arg;
    while (std::getline(in, arg, '\0')) args.push_back(arg);
    std::error_code ec;
    // Electron rewrites its command line as one string with spaces.
    if (args.size() == 1 && !fs::exists(args[0], ec)) {
        std::string whole = args[0];
        args.clear();
        size_t at = 0;
        while (at < whole.size()) {
            size_t end = whole.find(' ', at);
            if (end == std::string::npos) end = whole.size();
            if (end > at) args.push_back(whole.substr(at, end - at));
            at = end + 1;
        }
    }
    if (args.empty() || args[0].empty() || args[0][0] != '/') return {};
    for (const auto &a : args)
        if (!a.empty() && a[0] == '/' && !fs::exists(a, ec)) return {};
    return args;
}

// Closes Vesktop (like quitting it from the tray) and waits for it.
bool closeVesktop() {
    pid_t pid = vesktopPid();
    if (!pid) return false;
    g_vesktopCommand = reusableCommand(pid);
    std::printf("Closing Vesktop to apply the change...\n");
    kill(pid, SIGTERM);
    for (int i = 0; i < 100 && kill(pid, 0) == 0; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (kill(pid, 0) == 0) kill(pid, SIGKILL);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    return true;
}

void startVesktop() {
    // CATBERRY_VSKM_VESKTOP: another program to start (for tests).
    std::vector<std::string> args;
    if (!env("CATBERRY_VSKM_VESKTOP").empty()) args = {env("CATBERRY_VSKM_VESKTOP")};
    else if (!g_vesktopCommand.empty()) args = g_vesktopCommand;
    else args = {"vesktop"};
    std::vector<char *> argv;
    for (auto &a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    const std::string &program = args[0];
    pid_t pid;
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
    // Vesktop's own log would fill the terminal; it keeps running after we exit.
    posix_spawn_file_actions_t files;
    posix_spawn_file_actions_init(&files);
    posix_spawn_file_actions_addopen(&files, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&files, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&files, 2, "/dev/null", O_WRONLY, 0);
    if (posix_spawnp(&pid, program.c_str(), &files, &attr, argv.data(), environ) == 0) {
        // Wait until it holds its settings folder, so a next load/unload sees it.
        for (int i = 0; i < 100 && !vesktopPid(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::printf("Started Vesktop again.\n");
    } else
        std::printf("Start Vesktop again to use it.\n");
    posix_spawn_file_actions_destroy(&files);
    posix_spawnattr_destroy(&attr);
}

void usage(FILE *out) {
    std::fprintf(out,
                 "catberry vskm %s - Vencord plugins for Vesktop\n"
                 "\n"
                 "usage:\n"
                 "  catberry vskm -list            list the plugins and which are loaded\n"
                 "  catberry vskm load NAME        switch a plugin on (NAME: MF, TS, ACA or OD)\n"
                 "  catberry vskm unload NAME      switch it off\n"
                 "  catberry vskm unload all       switch every vskm plugin off\n"
                 "  catberry vskm -v               print the version\n"
                 "\n"
                 "A running Vesktop is closed and started again to apply the change.\n",
                 VSKM_VERSION);
}

int list() {
    json vencord;
    if (!readJson(vencordSettings(), vencord)) vencord = json::object();
    json vesktop;
    readJson(vesktopState(), vesktop);
    bool ours = vesktop.value("vencordDir", std::string()) == vencordDir().string();
    std::printf("catberry vskm %s plugins:\n\n", VSKM_VERSION);
    for (const Plugin &p : kPlugins) {
        bool on = ours && pluginOn(vencord, p);
        std::printf("  %-4s %-8s %s\n       %s\n", p.name, on ? "loaded" : "-", p.title, p.desc);
    }
    std::printf("\nLoad one with: catberry vskm load NAME\n");
    return 0;
}

// A Vencord folder made by scripts/build-vencord.sh (its SOURCE note says so).
bool isVskmBuild(const std::string &path) {
    if (path.empty()) return false;
    std::ifstream in(fs::path(path) / "SOURCE");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return text.find("vskm plugins") != std::string::npos;
}

// Switches one plugin on or off; with no plugin, switches every vskm plugin
// off (used before vskm is uninstalled).
int setPlugin(const Plugin *p, bool on) {
    std::error_code ec;
    fs::path dir = vencordDir();
    if (!p) {
        json vesktop, vencord;
        bool ours = fs::exists(vesktopState(), ec) && readJson(vesktopState(), vesktop) &&
                    vesktop.value("vencordDir", std::string()) == dir.string();
        bool anyOn = false;
        if (fs::exists(vencordSettings(), ec) && readJson(vencordSettings(), vencord))
            for (const Plugin &q : kPlugins) anyOn = anyOn || pluginOn(vencord, q);
        if (!ours && !anyOn) {
            std::printf("No vskm plugin is loaded.\n");
            return 0;
        }
    }
    if (!fs::exists(dir / "vencordDesktopMain.js", ec)) {
        std::fprintf(stderr, "catberry vskm: vskm's Vencord build is missing (%s). Reinstall: catberry install vskm --reinstall\n",
                     dir.c_str());
        return 1;
    }
    if (!fs::exists(configHome() / "vesktop", ec)) {
        std::fprintf(stderr, "catberry vskm: Vesktop's settings weren't found (%s).\nInstall Vesktop and open it once, then try again.\n",
                     (configHome() / "vesktop").c_str());
        return 1;
    }
    json vesktop, vencord, state;
    if (!readJson(vesktopState(), vesktop) || !readJson(vencordSettings(), vencord)) return 1;
    readJson(vskmState(), state);

    // Plugins being switched off put back what they changed (audio share
    // settings, Discord's GIF/emoji settings) when they stop; ask the running
    // Vesktop to stop them before closing it.
    if (!on && vesktopPid()) {
        std::string names;
        for (const Plugin &q : kPlugins)
            if ((!p || &q == p) && pluginOn(vencord, q)) names += std::string(q.vencord) + "\n";
        if (!names.empty()) {
            fs::create_directories(stopRequest().parent_path(), ec);
            std::ofstream(stopRequest()) << names;
            for (int i = 0; i < 80 && fs::exists(stopRequest(), ec); i++)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            fs::remove(stopRequest(), ec);
        }
    }
    bool wasRunning = closeVesktop();
    // Read again: closing Vesktop may have saved its settings.
    if (wasRunning && (!readJson(vesktopState(), vesktop) || !readJson(vencordSettings(), vencord))) return 1;

    if (!vencord.contains("plugins") || !vencord["plugins"].is_object()) vencord["plugins"] = json::object();
    for (const Plugin &q : kPlugins) {
        if (p && &q != p) continue;
        json &plugins = vencord["plugins"];
        if (!p && !plugins.contains(q.vencord)) continue;
        json &entry = plugins[q.vencord];
        if (!entry.is_object()) entry = json::object();
        entry["enabled"] = on;
    }

    bool anyOn = false;
    for (const Plugin &q : kPlugins) anyOn = anyOn || pluginOn(vencord, q);
    std::string current = vesktop.value("vencordDir", std::string());
    if (anyOn) {
        // Point Vesktop at vskm's Vencord, remembering what it used before.
        if (current != dir.string()) {
            // Another vskm build (eg a test copy) isn't Vesktop's own: keep
            // what was remembered from before it.
            if (!isVskmBuild(current)) state["previousVencordDir"] = current;
            vesktop["vencordDir"] = dir.string();
        }
    } else if (current == dir.string()) {
        // No vskm plugin left: back to Vesktop's own Vencord (or what it had).
        std::string previous = state.value("previousVencordDir", std::string());
        if (isVskmBuild(previous)) previous.clear();
        if (previous.empty()) vesktop.erase("vencordDir");
        else vesktop["vencordDir"] = previous;
    }

    // Misc Fixes' audio settings, if Vesktop wasn't there to put them back.
    json vsettings;
    bool mfOff = !on && (!p || std::string(p->name) == "MF");
    if (mfOff && fs::exists(vesktopSettings(), ec) && readJson(vesktopSettings(), vsettings) &&
        vsettings.contains("vskmPreviousAudio")) {
        json old = json::parse(vsettings.value("vskmPreviousAudio", std::string("{}")), nullptr, false);
        vsettings["audio"] = old.is_object() ? old : json::object();
        vsettings.erase("vskmPreviousAudio");
        writeJson(vesktopSettings(), vsettings);
    }
    // Theme Stealer's icon, if Vesktop wasn't there to put Vesktop's back:
    // its copy of Vesktop's launcher and its tray pictures.
    bool tsOff = !on && (!p || std::string(p->name) == "TS");
    if (tsOff) {
        std::string dataHome = env("XDG_DATA_HOME");
        fs::path launcher = (dataHome.empty() ? fs::path(env("HOME")) / ".local" / "share" : fs::path(dataHome)) /
                            "applications" / "vesktop.desktop";
        std::ifstream in(launcher);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.find("X-Vskm-Icon=true") != std::string::npos) fs::remove(launcher, ec);
        fs::path assets = configHome() / "vesktop" / "userAssets";
        if (fs::exists(assets / ".vskm-icon", ec)) {
            for (const char *f : {"tray", "trayUnread"}) {
                fs::remove(assets / f, ec);
                if (fs::exists(assets / (std::string(f) + ".vskm-backup"), ec))
                    fs::rename(assets / (std::string(f) + ".vskm-backup"), assets / f, ec);
            }
            fs::remove(assets / ".vskm-icon", ec);
        }
    }
    // OptiDiscord's media settings are Discord account settings: only a
    // running Discord can put them back.
    bool odOff = !on && (!p || std::string(p->name) == "OD");
    bool odPending = false;
    if (odOff && vencord["plugins"].contains("OptiDiscord")) {
        const json &od = vencord["plugins"]["OptiDiscord"];
        odPending = od.is_object() && !od.value("previousMedia", std::string()).empty();
    }

    bool ok = writeJson(vencordSettings(), vencord) && writeJson(vesktopState(), vesktop) && writeJson(vskmState(), state);
    if (ok && p) std::printf("%s (%s) %s.\n", p->title, p->name, on ? "loaded" : "unloaded");
    if (ok && !p) std::printf("All vskm plugins unloaded; Vesktop uses its own Vencord again.\n");
    if (ok && odPending)
        std::printf("OptiDiscord had turned off GIF autoplay and emoji/sticker animation; turn them back on in\n"
                    "Discord's Settings > Chat if you want them (Vesktop wasn't open to do it).\n");
    if (wasRunning) startVesktop();
    else if (ok && on) std::printf("Open Vesktop to use it.\n");
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
    std::string command = argc > 1 ? argv[1] : "";
    if (command == "-v" || command == "--version") {
        std::printf("catberry vskm %s\n", VSKM_VERSION);
        return 0;
    }
    if (command == "-list" || command == "--list" || command == "list") return list();
    if (command == "load" || command == "unload") {
        if (argc < 3) {
            std::fprintf(stderr, "catberry vskm: which plugin? (MF, TS, ACA or OD)\n");
            return 2;
        }
        bool all = command == "unload" && (std::string(argv[2]) == "all" || std::string(argv[2]) == "-all");
        const Plugin *p = all ? nullptr : findPlugin(argv[2]);
        if (!p && !all) {
            std::fprintf(stderr, "catberry vskm: no plugin called '%s'. See: catberry vskm -list\n", argv[2]);
            return 2;
        }
        return setPlugin(p, command == "load");
    }
    if (command.empty() || command == "-help" || command == "--help" || command == "help") {
        usage(stdout);
        return 0;
    }
    std::fprintf(stderr, "catberry vskm: unknown command '%s'\n\n", command.c_str());
    usage(stderr);
    return 2;
}
