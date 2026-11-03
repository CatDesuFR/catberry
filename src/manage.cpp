#include "manage.h"

#include "fetch.h"
#include "process.h"
#include "releases.h"
#include "system.h"
#include "tools.h"
#include "version.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

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
bool publishedVersions(const std::string &tool, const TempDir &tmp, std::vector<std::string> &out) {
    std::string file = tmp.path() + "/releases.json";
    if (!download(releaseListUrl(), file)) {
        std::fprintf(stderr, "catberry: could not get the list of releases from GitHub (no internet connection?)\n");
        return false;
    }
    out = toolVersions(readFile(file), tool);
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

// Downloads, checks and installs one package. `installed` is the version
// there now ("" if none), to choose dnf's downgrade.
int installPackage(const SystemInfo &sys, const Tool &tool, const std::string &version, const std::string &installed,
                   const TempDir &tmp) {
    const std::string tag = std::string(tool.name) + "-v" + version;
    const std::string sumsFile = tmp.path() + "/SHA256SUMS-" + tag;
    SumEntry asset;
    if (!download(assetUrl(tag, "SHA256SUMS"), sumsFile) ||
        !findAsset(readFile(sumsFile), tool.program, assetSuffix(sys.family), asset)) {
        std::fprintf(stderr, "catberry: %s %s has no package for this system.\n", tool.name, version.c_str());
        return 1;
    }
    const std::string file = tmp.path() + "/" + asset.name;
    if (!download(assetUrl(tag, asset.name), file) || !sha256Matches(file, asset.sha256)) {
        std::fprintf(stderr, "catberry: download damaged, nothing was installed.\n");
        return 1;
    }

    std::vector<std::string> command;
    switch (sys.family) {
    case Family::Arch: command = {"pacman", "-U", file}; break;
    case Family::Fedora:
        command = {"dnf", !installed.empty() && compareText(version, installed) < 0 ? "downgrade" : "install", file};
        break;
    default: command = {"apt", "install", "--allow-downgrades", file}; break;
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

    std::string installed = installedVersion(*tool);
    bool isLauncher = name == "catberry";
    // The launcher always reports its own version; for it, what counts is the package.
    bool sameInstalled = !installed.empty() && compareText(installed, version) == 0 &&
                         (!isLauncher || installedAsPackage(sys.family, tool->program));
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
        if (int code = installPackage(sys, *launcher, launcherVersions.front(), "", tmp)) return code;
    }

    if (int code = installPackage(sys, *tool, version, installed, tmp)) return code;
    std::printf("%s %s installed.\n", tool->name, version.c_str());
    if (name == "roofce")
        std::printf("If the wallpaper is running, restart it: catberry roofce stop; catberry roofce start\n");
    return 0;
}

int uninstallTool(const std::string &) {
    std::fprintf(stderr, "catberry: uninstall is not available yet\n");
    return 2;
}

#else

int installTool(const std::string &, const std::string &, bool) {
    std::fprintf(stderr, "catberry: install is not available on Windows yet\n");
    return 2;
}

int uninstallTool(const std::string &) {
    std::fprintf(stderr, "catberry: uninstall is not available on Windows yet\n");
    return 2;
}

#endif
