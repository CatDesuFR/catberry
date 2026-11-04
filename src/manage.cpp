#include "manage.h"

#include "fetch.h"
#include "process.h"
#include "releases.h"
#include "system.h"
#include "tools.h"
#include "version.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <sstream>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

// Where the releases are read from. CATBERRY_RELEASES points at a copy
// (releases.json and <tag>/<file>), used by the tests.
std::string releaseListUrl() {
    const char *base = std::getenv("CATBERRY_RELEASES");
    if (base && *base) return std::string(base) + "/releases.json";
    return "https://api.github.com/repos/CatDesuFR/catberry/releases?per_page=100";
}

std::string assetUrl(const std::string &tag, const std::string &name) {
    const char *base = std::getenv("CATBERRY_RELEASES");
    if (base && *base) return std::string(base) + "/" + tag + "/" + name;
    return "https://github.com/CatDesuFR/catberry/releases/download/" + tag + "/" + name;
}

std::string readFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

std::string joinList(const std::vector<std::string> &items) {
    std::string out;
    for (const std::string &s : items) out += (out.empty() ? "" : ", ") + s;
    return out;
}

// The published versions of a tool, newest first; false (with a message)
// when GitHub cannot be reached.
const char *const kRepoName = "CatDesuFR/catberry";

// Set once GitHub's public download has refused this run and the gh
// command (logged in) is used instead.
bool g_viaGh = false;

bool ghLoggedIn() { return captureOutputQuiet({"gh", "auth", "status"}); }

bool ghTags(std::vector<std::string> &tags) {
    std::string out;
    if (!captureOutput({"gh", "release", "list", "-R", kRepoName, "--limit", "200", "--json", "tagName", "--jq",
                        ".[].tagName"},
                       out))
        return false;
    std::istringstream lines(out);
    std::string line;
    while (std::getline(lines, line))
        if (!line.empty()) tags.push_back(line);
    return true;
}

// One file of a release, the same way the list was got.
bool fetchAsset(const std::string &tag, const std::string &name, const std::string &file) {
    if (g_viaGh)
        return captureOutputQuiet({"gh", "release", "download", tag, "-R", kRepoName, "-p", name, "-O", file,
                                   "--clobber"});
    return download(assetUrl(tag, name), file);
}

// The published versions of a tool, newest first; false (with a message)
// when GitHub cannot be reached.
bool publishedVersions(const std::string &tool, const TempDir &tmp, std::vector<std::string> &out) {
    const std::string file = tmp.path() + "/releases.json";
    int status = 0;
    if (g_viaGh) {
        std::vector<std::string> tags;
        if (!ghTags(tags)) {
            std::fprintf(stderr, "catberry: gh could not list the releases.\n");
            return false;
        }
        out = versionsFromTags(tags, tool);
    } else if (fetchUrl(releaseListUrl(), file, status)) {
        out = toolVersions(readFile(file), tool);
    } else {
        // GitHub refused (a hidden account, or limits): gh, when logged in,
        // can still see the releases. Not for a test copy (CATBERRY_RELEASES).
        const char *testCopy = std::getenv("CATBERRY_RELEASES");
        std::vector<std::string> tags;
        if (status != 0 && !(testCopy && *testCopy) && ghLoggedIn() && ghTags(tags)) {
            g_viaGh = true;
            std::printf("GitHub's public downloads are not available (HTTP %d); using your gh login instead.\n",
                        status);
            out = versionsFromTags(tags, tool);
        } else if (status == 404) {
            std::fprintf(stderr,
                         "catberry: GitHub says the catberry releases are not there (HTTP 404). The repository may "
                         "be hidden or private: https://github.com/%s/releases\n"
                         "If you can see them there while logged in, install and log in to gh (GitHub's command "
                         "line tool), and catberry uses it.\n",
                         kRepoName);
            return false;
        } else if (status != 0) {
            std::fprintf(stderr, "catberry: GitHub refused the list of releases (HTTP %d). Try again later.\n", status);
            return false;
        } else {
            std::fprintf(stderr, "catberry: could not reach GitHub (no internet connection?)\n");
            return false;
        }
    }
    if (out.empty()) {
        std::fprintf(stderr, "catberry: %s has no published versions yet.\n", tool.c_str());
        return false;
    }
    return true;
}

// "latest" or a version written by the user, as one of the published ones.
bool resolveVersion(const std::string &tool, const std::string &wanted, const std::vector<std::string> &published,
                    std::string &out) {
    if (wanted == "latest") {
        out = published.front();
        return true;
    }
    Version w;
    if (parseVersion(wanted, w))
        for (const std::string &p : published) {
            Version v;
            if (parseVersion(p, v) && compareVersions(v, w) == 0) {
                out = p;
                return true;
            }
        }
    std::string shown = wanted.size() > 1 && (wanted[0] == 'v' || wanted[0] == 'V') ? wanted.substr(1) : wanted;
    std::fprintf(stderr, "%s has no version %s. Published: %s\n", tool.c_str(), shown.c_str(),
                 joinList(published).c_str());
    return false;
}

// The version installed now, "" if none.
std::string installedVersion(const Tool &tool) {
    if (std::string(tool.name) == "catberry") return kVersion;
    std::string path = findTool(tool.program);
    return path.empty() ? "" : toolVersion(path);
}

int compareText(const std::string &a, const std::string &b) {
    Version va, vb;
    if (!parseVersion(a, va) || !parseVersion(b, vb)) return 0;
    return compareVersions(va, vb);
}

#ifndef _WIN32

SystemInfo thisSystem() {
    std::string text = readFile("/etc/os-release");
    if (text.empty()) text = readFile("/usr/lib/os-release");
    return detectSystem(text);
}

bool installedAsPackage(Family family, const std::string &program) {
    std::string out;
    switch (family) {
    case Family::Arch: return captureOutput({"pacman", "-Q", program}, out);
    case Family::Fedora: return captureOutput({"rpm", "-q", program}, out);
    case Family::Debian13:
    case Family::Ubuntu:
        return captureOutput({"dpkg-query", "-W", "-f=${Status}", program}, out) && out == "install ok installed";
    default: return false;
    }
}

// The version of the installed package ("1.1.2" from "1.1.2-1+debian13"),
// or "" when it is not installed as a package.
std::string packageVersion(Family family, const std::string &program) {
    std::string out;
    bool ok = false;
    switch (family) {
    case Family::Arch:
        ok = captureOutput({"pacman", "-Q", program}, out); // "catberry-ac 1.1.2-1"
        if (ok) out = out.substr(out.find(' ') + 1);
        break;
    case Family::Fedora: ok = captureOutput({"rpm", "-q", "--qf", "%{VERSION}", program}, out); break;
    case Family::Debian13:
    case Family::Ubuntu:
        ok = installedAsPackage(family, program) && captureOutput({"dpkg-query", "-W", "-f=${Version}", program}, out);
        break;
    default: break;
    }
    if (!ok) return "";
    out = out.substr(0, out.find_first_of("-\n"));
    return out;
}

// The version installed now: the package's, else what the program says.
std::string currentVersion(const Tool &tool) {
    std::string version = packageVersion(thisSystem().family, tool.program);
    if (!version.empty()) return version;
    return installedVersion(tool); // the launcher: this program's own
}

// Downloads, checks and installs one package. `installed` is the version
// there now ("" if none), to choose dnf's downgrade; `same` means this exact
// version is already installed as a package (a --reinstall), which dnf and
// apt otherwise skip as done.
int installPackage(const SystemInfo &sys, const Tool &tool, const std::string &version, const std::string &installed,
                   bool same, const TempDir &tmp) {
    const std::string tag = std::string(tool.name) + "-v" + version;
    const std::string sumsFile = tmp.path() + "/SHA256SUMS-" + tag;
    SumEntry asset;
    if (!fetchAsset(tag, "SHA256SUMS", sumsFile) ||
        !findAsset(readFile(sumsFile), tool.program, assetSuffix(sys.family), asset)) {
        std::fprintf(stderr, "catberry: %s %s has no package for this system.\n", tool.name, version.c_str());
        return 1;
    }
    const std::string file = tmp.path() + "/" + asset.name;
    if (!fetchAsset(tag, asset.name, file) || !sha256Matches(file, asset.sha256)) {
        std::fprintf(stderr, "catberry: download damaged, nothing was installed.\n");
        return 1;
    }

    std::vector<std::string> command;
    switch (sys.family) {
    case Family::Arch: command = {"pacman", "-U", file}; break;
    case Family::Fedora:
        command = {"dnf",
                   same                                                          ? "reinstall"
                   : !installed.empty() && compareText(version, installed) < 0 ? "downgrade"
                                                                                 : "install",
                   file};
        break;
    default:
        command = same ? std::vector<std::string>{"apt", "install", "--reinstall", "--allow-downgrades", file}
                       : std::vector<std::string>{"apt", "install", "--allow-downgrades", file};
        break;
    }
    int code = runProgram(withSudo(command));
    if (code != 0) {
        std::fprintf(stderr, "catberry: %s failed; nothing more was changed.\n", command[0].c_str());
        return code;
    }
    return 0;
}

#endif

} // namespace

int listVersions(const std::string &name) {
    const Tool *tool = toolByName(name);
    if (!tool) return 2;
    TempDir tmp;
    std::vector<std::string> published;
    if (!publishedVersions(name, tmp, published)) return 1;
    std::string installed = installedVersion(*tool);
    std::printf("%s versions:\n", tool->name);
    for (const std::string &v : published)
        std::printf("  %-7s%s\n", v.c_str(), compareText(v, installed) == 0 && !installed.empty() ? " (installed)" : "");
    return 0;
}

#ifndef _WIN32

int installTool(const std::string &name, const std::string &wanted, bool reinstall) {
    const Tool *tool = toolByName(name);
    if (!tool) return 2;
    SystemInfo sys = thisSystem();
    if (sys.family == Family::Unsupported) {
        std::fprintf(stderr,
                     "catberry install supports Arch-based distros, Fedora, Debian 13 and Ubuntu 26.04 or newer. "
                     "This system (%s) is not one of them.\n",
                     sys.prettyName.empty() ? "unknown" : sys.prettyName.c_str());
        return 1;
    }

    TempDir tmp;
    std::vector<std::string> published;
    std::string version;
    if (!publishedVersions(name, tmp, published) || !resolveVersion(name, wanted, published, version)) return 1;

    // What the package manager has installed counts first: the program
    // itself may be missing (a broken install), and the running launcher
    // always reports its own version.
    std::string installed = packageVersion(sys.family, tool->program);
    bool asPackage = !installed.empty();
    if (!asPackage && name != "catberry") installed = installedVersion(*tool);
    bool sameInstalled = !installed.empty() && compareText(installed, version) == 0;
    if (sameInstalled && !reinstall) {
        std::printf("%s %s is already installed.\n", tool->name, version.c_str());
        return 0;
    }

    // ac and mtools are packaged to need the launcher's package.
    if ((name == "ac" || name == "mtools") && !installedAsPackage(sys.family, "catberry")) {
        const Tool *launcher = toolByName("catberry");
        std::vector<std::string> launcherVersions;
        if (!publishedVersions("catberry", tmp, launcherVersions)) return 1;
        std::printf("%s needs the catberry package; installing catberry %s first.\n", tool->name,
                    launcherVersions.front().c_str());
        if (int code = installPackage(sys, *launcher, launcherVersions.front(), "", false, tmp)) return code;
    }

    bool samePackage = sameInstalled && asPackage;
    if (int code = installPackage(sys, *tool, version, installed, samePackage, tmp)) return code;
    std::printf("%s %s installed.\n", tool->name, version.c_str());
    if (name == "roofce")
        std::printf("If the wallpaper is running, restart it: catberry roofce stop; catberry roofce start\n");
    return 0;
}

namespace {

std::string envDir(const char *var, const char *fallbackUnderHome) {
    const char *v = std::getenv(var);
    if (v && *v) return v;
    const char *home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/" + fallbackUnderHome;
}
std::string configHome() { return envDir("XDG_CONFIG_HOME", ".config"); }
std::string dataHome() { return envDir("XDG_DATA_HOME", ".local/share"); }
std::string cacheHome() { return envDir("XDG_CACHE_HOME", ".cache"); }
std::string userBin() {
    const char *home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.local/bin";
}

bool exists(const std::string &path) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(path, ec));
}

// The user's own settings and files of a tool (only the ones that exist).
std::vector<std::string> personalFiles(const std::string &tool) {
    std::vector<std::string> all;
    const std::string cfg = configHome() + "/catberry";
    if (tool == "ac") all = {cfg + "/ac.conf"};
    if (tool == "mtools") all = {cfg + "/mtools.conf", cfg + "/macros", cfg + "/configs"};
    if (tool == "roofce")
        all = {cfg + "/roofce.conf", dataHome() + "/catberry/roofce", cacheHome() + "/catberry/roofce",
               dataHome() + "/fonts/catberry"};
    std::vector<std::string> found;
    for (const std::string &p : all)
        if (exists(p)) found.push_back(p);
    return found;
}

// Copies of the program outside the package manager.
std::vector<std::string> looseCopies(const Tool &tool) {
    std::vector<std::string> all = {userBin() + "/" + tool.program};
    // The app-menu entry that make install puts in ~/.local.
    if (std::string(tool.name) != "catberry") all.push_back(dataHome() + "/applications/" + tool.program + ".desktop");
    if (std::string(tool.name) == "roofce") all.push_back(configHome() + "/autostart/catberry-roofce.desktop");
    std::vector<std::string> found;
    for (const std::string &p : all)
        if (exists(p)) found.push_back(p);
    return found;
}

bool isInstalled(Family family, const Tool &tool) {
    return installedAsPackage(family, tool.program) || !looseCopies(tool).empty();
}

// Reads one answer line straight from stdin (unbuffered, so nothing meant
// for a later question is swallowed). Only "y"/"yes" is yes; no input is No.
bool askYesNo(const std::string &question) {
    std::printf("%s [y/N] ", question.c_str());
    std::fflush(stdout);
    std::string line;
    char c;
    while (read(0, &c, 1) == 1 && c != '\n') line += c;
    std::printf("\n");
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return line == "y" || line == "Y" || line == "yes" || line == "Yes";
}

bool running(const char *program) {
    std::string out;
    return captureOutput({"pgrep", "-x", program}, out);
}

// Stops the tool if it is running (errors ignored).
void stopTool(const Tool &tool) {
    std::string name = tool.name;
    // The launcher is this very process, uninstalling.
    if (name == "catberry" || !running(tool.program)) return;
    std::string path = findTool(tool.program);
    if (name == "roofce" && !path.empty()) runProgram({path, "stop"});
    if (name == "mtools" && !path.empty()) runProgram({path, "-configunload"});
    runProgram({"pkill", "-x", tool.program});
}

// Removes the package and every loose copy. True when anything was there.
bool removeCopies(const SystemInfo &sys, const Tool &tool, int &code) {
    bool found = false;
    stopTool(tool);
    if (installedAsPackage(sys.family, tool.program)) {
        found = true;
        std::vector<std::string> command;
        switch (sys.family) {
        case Family::Arch: command = {"pacman", "-R", "--noconfirm", tool.program}; break;
        case Family::Fedora: command = {"dnf", "remove", "-y", tool.program}; break;
        default: command = {"apt", "remove", "-y", tool.program}; break;
        }
        int c = runProgram(withSudo(command));
        if (c != 0) {
            std::fprintf(stderr, "catberry: %s failed.\n", command[0].c_str());
            code = c;
        }
    }
    for (const std::string &p : looseCopies(tool)) {
        found = true;
        std::error_code ec;
        fs::remove(p, ec);
        if (ec) {
            std::fprintf(stderr, "catberry: could not remove %s: %s\n", p.c_str(), ec.message().c_str());
            code = 1;
        }
    }
    return found;
}

// Asks once about all the files, deletes them on yes. True when kept.
bool offerPersonalFiles(const std::vector<std::string> &files) {
    if (files.empty()) return false;
    std::printf("Your settings and files:\n");
    for (const std::string &f : files) std::printf("  %s\n", f.c_str());
    if (!askYesNo("Delete these too?")) return true;
    for (const std::string &f : files) {
        std::error_code ec;
        fs::remove_all(f, ec);
        if (ec) std::fprintf(stderr, "catberry: could not delete %s: %s\n", f.c_str(), ec.message().c_str());
    }
    // Our folders, if nothing else is left in them.
    for (const std::string &dir : {configHome() + "/catberry", dataHome() + "/catberry", cacheHome() + "/catberry"}) {
        std::error_code ec;
        if (fs::is_directory(dir, ec) && fs::is_empty(dir, ec)) fs::remove(dir, ec);
    }
    return false;
}

} // namespace

int uninstallTool(const std::string &name) {
    const Tool *tool = toolByName(name);
    if (!tool) return 2;
    // Under sudo, HOME is root's: the user's own copies and files would be missed.
    const char *sudoUser = std::getenv("SUDO_USER");
    if (isRoot() && sudoUser && *sudoUser) {
        std::fprintf(stderr, "catberry: Run it without sudo; it asks for your password when it needs it.\n");
        return 1;
    }
    SystemInfo sys = thisSystem();
    int code = 0;

    if (name != "catberry") {
        bool found = removeCopies(sys, *tool, code);
        if (found)
            std::printf("%s uninstalled.\n", tool->name);
        else
            std::printf("%s is not installed.\n", tool->name);
        if (offerPersonalFiles(personalFiles(name))) std::printf("Your settings and files were kept.\n");
        return code;
    }

    // The launcher: ac and mtools cannot run without it; roofce can.
    std::vector<const Tool *> chosen;
    std::vector<std::string> needing;
    for (const char *dep : {"ac", "mtools"})
        if (isInstalled(sys.family, *toolByName(dep))) needing.push_back(dep);
    if (!needing.empty()) {
        std::string who = needing.size() == 2 ? "ac and mtools need" : needing[0] + " needs";
        if (!askYesNo(who + " the launcher and will be removed too. Continue?")) {
            std::printf("Nothing was changed.\n");
            return 0;
        }
        for (const std::string &dep : needing) chosen.push_back(toolByName(dep));
    }
    if (isInstalled(sys.family, *toolByName("roofce")) && askYesNo("Also remove roofce?"))
        chosen.push_back(toolByName("roofce"));

    std::vector<std::string> files;
    for (const Tool *t : chosen) {
        removeCopies(sys, *t, code);
        std::printf("%s uninstalled.\n", t->name);
        for (const std::string &f : personalFiles(t->name)) files.push_back(f);
    }
    bool found = removeCopies(sys, *tool, code);
    std::printf(found ? "catberry uninstalled.\n" : "catberry is not installed as a package.\n");
    if (offerPersonalFiles(files)) std::printf("Your settings and files were kept.\n");
    return code;
}

#else // Windows

namespace {

std::string envOr(const char *var, const std::string &fallback) {
    const char *v = std::getenv(var);
    return v && *v ? std::string(v) : fallback;
}
// The folder install.bat uses, shared by all catberry programs.
std::string installDir() { return envOr("LOCALAPPDATA", ".") + "\\Programs\\catberry"; }
std::string configDir() { return envOr("APPDATA", ".") + "\\catberry"; }
std::string exePath(const Tool &tool) { return installDir() + "\\" + tool.program + ".exe"; }

std::string currentVersion(const Tool &tool) { return installedVersion(tool); }

bool linuxOnly(const std::string &name) {
    if (name != "roofce") return false;
    std::fprintf(stderr, "roofce is Linux-only (Wayland).\n");
    return true;
}

bool askYesNo(const std::string &question) {
    std::printf("%s [y/N] ", question.c_str());
    std::fflush(stdout);
    std::string line;
    int c;
    while ((c = std::fgetc(stdin)) != EOF && c != '\n') line += char(c);
    std::printf("\n");
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return line == "y" || line == "Y" || line == "yes" || line == "Yes";
}

// Adds the install folder to the user PATH, the same way install.bat does.
void addToPath() {
    runProgram({"powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                "$dir = Join-Path $env:LOCALAPPDATA 'Programs\\catberry';"
                "$path = [Environment]::GetEnvironmentVariable('Path', 'User');"
                "$parts = @($path -split ';' | Where-Object { $_ -ne '' });"
                "if ($parts -notcontains $dir) { [Environment]::SetEnvironmentVariable('Path', (($parts + $dir) -join "
                "';'), 'User'); 'Added catberry to your PATH. Open a new terminal to use it.' }"});
}

void removeFromPath() {
    runProgram({"powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                "$dir = Join-Path $env:LOCALAPPDATA 'Programs\\catberry';"
                "$path = [Environment]::GetEnvironmentVariable('Path', 'User');"
                "$parts = @($path -split ';' | Where-Object { $_ -ne '' -and $_ -ne $dir });"
                "[Environment]::SetEnvironmentVariable('Path', ($parts -join ';'), 'User')"});
}

std::vector<std::string> personalFiles(const std::string &tool) {
    std::vector<std::string> all;
    const std::string cfg = configDir();
    if (tool == "ac") all = {cfg + "\\ac.conf"};
    if (tool == "mtools") all = {cfg + "\\mtools.conf", cfg + "\\macros", cfg + "\\configs"};
    std::vector<std::string> found;
    std::error_code ec;
    for (const std::string &p : all)
        if (fs::exists(p, ec)) found.push_back(p);
    return found;
}

bool offerPersonalFiles(const std::vector<std::string> &files) {
    if (files.empty()) return false;
    std::printf("Your settings and files:\n");
    for (const std::string &f : files) std::printf("  %s\n", f.c_str());
    if (!askYesNo("Delete these too?")) return true;
    std::error_code ec;
    for (const std::string &f : files) fs::remove_all(f, ec);
    if (fs::is_directory(configDir(), ec) && fs::is_empty(configDir(), ec)) fs::remove(configDir(), ec);
    return false;
}

// Stops the tool and deletes its exe. True when it was there.
bool removeExe(const Tool &tool, int &code) {
    std::string exe = exePath(tool);
    std::error_code ec;
    if (!fs::exists(exe, ec)) return false;
    std::string ignored;
    if (std::string(tool.name) == "mtools") runProgram({exe, "-configunload"});
    captureOutput({"taskkill", "/im", std::string(tool.program) + ".exe", "/f"}, ignored);
    fs::remove(exe, ec);
    if (ec) {
        std::fprintf(stderr, "catberry: could not delete %s: %s\n", exe.c_str(), ec.message().c_str());
        code = 1;
    }
    return true;
}

} // namespace

int installTool(const std::string &name, const std::string &wanted, bool reinstall) {
    const Tool *tool = toolByName(name);
    if (!tool || linuxOnly(name)) return tool ? 1 : 2;
    TempDir tmp;
    std::vector<std::string> published;
    std::string version;
    if (!publishedVersions(name, tmp, published) || !resolveVersion(name, wanted, published, version)) return 1;
    std::string installed = installedVersion(*tool);
    if (!installed.empty() && compareText(installed, version) == 0 && !reinstall) {
        std::printf("%s %s is already installed.\n", tool->name, version.c_str());
        return 0;
    }

    const std::string tag = name + "-v" + version, sums = tmp.path() + "\\SHA256SUMS";
    SumEntry asset;
    if (!fetchAsset(tag, "SHA256SUMS", sums) || !findAsset(readFile(sums), tool->program, "-windows.zip", asset)) {
        std::fprintf(stderr, "catberry: %s %s has no Windows download.\n", tool->name, version.c_str());
        return 1;
    }
    const std::string zip = tmp.path() + "\\" + asset.name;
    if (!fetchAsset(tag, asset.name, zip) || !sha256Matches(zip, asset.sha256)) {
        std::fprintf(stderr, "catberry: download damaged, nothing was installed.\n");
        return 1;
    }
    if (runProgram({"tar.exe", "-xf", zip, "-C", tmp.path()}) != 0) {
        std::fprintf(stderr, "catberry: could not unpack %s; nothing was installed.\n", asset.name.c_str());
        return 1;
    }
    const std::string fresh = tmp.path() + "\\" + tool->program + "-windows\\" + tool->program + ".exe";
    std::error_code ec;
    fs::create_directories(installDir(), ec);
    const std::string dest = exePath(*tool);
    // A running exe cannot be replaced, only renamed: the launcher moves
    // itself aside (deleted on its next start); a tool is stopped.
    if (name == "catberry") {
        fs::remove(installDir() + "\\catberry.old.exe", ec);
        if (fs::exists(dest, ec)) fs::rename(dest, installDir() + "\\catberry.old.exe", ec);
    } else {
        std::string ignored;
        captureOutput({"taskkill", "/im", std::string(tool->program) + ".exe", "/f"}, ignored);
    }
    fs::copy_file(fresh, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::fprintf(stderr, "catberry: could not copy %s to %s: %s\n", fresh.c_str(), dest.c_str(), ec.message().c_str());
        return 1;
    }
    addToPath();
    std::printf("%s %s installed.\n", tool->name, version.c_str());
    return 0;
}

int uninstallTool(const std::string &name) {
    const Tool *tool = toolByName(name);
    if (!tool || linuxOnly(name)) return tool ? 1 : 2;
    int code = 0;
    std::error_code ec;

    if (name != "catberry") {
        bool found = removeExe(*tool, code);
        std::printf(found ? "%s uninstalled.\n" : "%s is not installed.\n", tool->name);
        if (offerPersonalFiles(personalFiles(name))) std::printf("Your settings and files were kept.\n");
        return code;
    }

    std::vector<std::string> needing;
    for (const char *dep : {"ac", "mtools"})
        if (fs::exists(exePath(*toolByName(dep)), ec)) needing.push_back(dep);
    if (!needing.empty()) {
        std::string who = needing.size() == 2 ? "ac and mtools need" : needing[0] + " needs";
        if (!askYesNo(who + " the launcher and will be removed too. Continue?")) {
            std::printf("Nothing was changed.\n");
            return 0;
        }
    }
    std::vector<std::string> files;
    for (const std::string &dep : needing) {
        removeExe(*toolByName(dep), code);
        std::printf("%s uninstalled.\n", dep.c_str());
        for (const std::string &f : personalFiles(dep)) files.push_back(f);
    }
    // This exe is running: move it aside now; it is deleted (and the folder,
    // if then empty) by a separate process after this one has ended.
    const std::string dest = exePath(*tool), old = installDir() + "\\catberry.old.exe";
    bool movedAside = false;
    if (fs::exists(dest, ec)) {
        fs::remove(old, ec);
        fs::rename(dest, old, ec);
        movedAside = !ec;
    }
    bool othersLeft = false;
    for (const auto &entry : fs::directory_iterator(installDir(), ec))
        if (entry.path().filename() != "catberry.old.exe") othersLeft = true;
    if (!othersLeft) removeFromPath();
    std::printf("catberry uninstalled.\n");
    if (offerPersonalFiles(files)) std::printf("Your settings and files were kept.\n");

    // Last of all, so no question keeps this exe running past the delete.
    // Tries once a second for up to a minute, until this exe has exited.
    if (movedAside) {
        std::string line = "cmd.exe /c for /l %i in (1,1,60) do @(if exist \"" + old +
                           "\" (ping -n 2 127.0.0.1 >nul & del /f /q \"" + old + "\" 2>nul)) & rmdir \"" +
                           installDir() + "\" 2>nul";
        STARTUPINFOA startup{};
        startup.cb = sizeof startup;
        PROCESS_INFORMATION info{};
        if (CreateProcessA(nullptr, &line[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr,
                           nullptr, &startup, &info)) {
            CloseHandle(info.hProcess);
            CloseHandle(info.hThread);
        }
    }
    return code;
}

#endif

namespace {

// "[Y/n]": Enter, or no input at all, means yes. Reads straight from stdin,
// so the package manager's own questions get the rest.
bool askYesDefault(const std::string &question) {
    std::printf("%s [Y/n] ", question.c_str());
    std::fflush(stdout);
    std::string line;
#ifdef _WIN32
    int c;
    while ((c = std::fgetc(stdin)) != EOF && c != '\n') line += char(c);
#else
    char c;
    while (read(0, &c, 1) == 1 && c != '\n') line += c;
#endif
    std::printf("\n");
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return line.empty() || line == "y" || line == "Y" || line == "yes" || line == "Yes";
}

} // namespace

int updateTools(const std::string &only) {
    std::vector<const Tool *> tools;
    if (!only.empty()) {
        if (const Tool *t = toolByName(only)) tools.push_back(t);
    } else {
        // kTools ends with the launcher: it updates last, being the program doing it.
        for (const Tool &t : kTools) {
#ifdef _WIN32
            if (std::string(t.name) == "roofce") continue; // Linux-only
#endif
            tools.push_back(&t);
        }
    }
    if (tools.empty()) return 2;

    struct Update {
        const Tool *tool;
        std::string from, to;
    };
    std::vector<Update> updates;
    std::vector<std::string> notChecked;
    TempDir tmp;
    std::printf("Checking for updates:\n");
    for (const Tool *t : tools) {
        std::string current = currentVersion(*t);
        if (current.empty()) {
            if (!only.empty()) {
                std::fprintf(stderr, "%s is not installed. Install it with: catberry install %s\n", t->name, t->name);
                return 1;
            }
            continue;
        }
        std::vector<std::string> published;
        if (!publishedVersions(t->name, tmp, published)) {
            if (!only.empty()) return 1;
            // GitHub unreachable: the others would fail the same way.
            notChecked.push_back(t->name);
            for (const Tool *rest : tools)
                if (rest > t && !currentVersion(*rest).empty()) notChecked.push_back(rest->name);
            break;
        }
        const std::string &latest = published.front();
        if (compareText(latest, current) > 0) {
            updates.push_back({t, current, latest});
            std::printf("  %-8s %s -> %s\n", t->name, current.c_str(), latest.c_str());
        } else {
            std::printf("  %-8s %s (up to date)\n", t->name, current.c_str());
        }
    }
    if (!notChecked.empty()) {
        std::fprintf(stderr, "catberry: could not check %s for updates.\n", joinList(notChecked).c_str());
        if (updates.empty()) return 1;
    }
    if (updates.empty()) {
        std::printf(only.empty() ? "Everything is up to date.\n" : "%s is up to date.\n", only.c_str());
        return 0;
    }
    if (!askYesDefault(updates.size() == 1 ? "Update it?" : "Update them?")) {
        std::printf("Nothing was changed.\n");
        return 0;
    }
    std::vector<std::string> failed;
    for (const Update &u : updates)
        if (installTool(u.tool->name, u.to, false) != 0) failed.push_back(u.tool->name);
    if (!failed.empty()) {
        std::fprintf(stderr, "catberry: not updated: %s\n", joinList(failed).c_str());
        return 1;
    }
    return 0;
}

#ifdef _WIN32
void removeOldLauncher() {
    // Left behind when the launcher replaced itself.
    std::error_code ec;
    fs::remove(installDir() + "\\catberry.old.exe", ec);
}
#else
void removeOldLauncher() {}
#endif
