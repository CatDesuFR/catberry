#include "system.h"

#include "version.h"

#include <map>
#include <sstream>

namespace {

std::map<std::string, std::string> parseOsRelease(const std::string &text) {
    std::map<std::string, std::string> out;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        std::string value = line.substr(eq + 1);
        if (value.size() >= 2 && (value[0] == '"' || value[0] == '\'') && value.back() == value[0])
            value = value.substr(1, value.size() - 2);
        out[line.substr(0, eq)] = value;
    }
    return out;
}

bool hasWord(const std::string &words, const std::string &word) {
    std::istringstream in(words);
    std::string w;
    while (in >> w)
        if (w == word) return true;
    return false;
}

// Ubuntu releases before 26.04, by code name.
bool olderUbuntu(const std::string &codename) {
    for (const char *old : {"focal", "jammy", "noble", "oracular", "plucky", "questing"})
        if (codename == old) return true;
    return false;
}

} // namespace

SystemInfo detectSystem(const std::string &osRelease) {
    auto os = parseOsRelease(osRelease);
    SystemInfo info;
    info.prettyName = os.count("PRETTY_NAME") ? os["PRETTY_NAME"] : os["NAME"];
    const std::string id = os["ID"], like = os["ID_LIKE"];
    auto is = [&](const char *name) { return id == name || hasWord(like, name); };

    if (is("arch")) {
        info.family = Family::Arch;
    } else if (is("fedora")) {
        info.family = Family::Fedora;
    } else if (id == "ubuntu") {
        Version v, min;
        parseVersion("26.04", min);
        if (parseVersion(os["VERSION_ID"], v) && compareVersions(v, min) >= 0) info.family = Family::Ubuntu;
    } else if (hasWord(like, "ubuntu")) {
        const std::string codename = os["UBUNTU_CODENAME"];
        if (!codename.empty() && !olderUbuntu(codename)) info.family = Family::Ubuntu;
    } else if (id == "debian" && os["VERSION_ID"] == "13") {
        info.family = Family::Debian13;
    }
    return info;
}

const char *assetSuffix(Family family) {
    switch (family) {
    case Family::Arch: return ".pkg.tar.zst";
    case Family::Fedora: return ".rpm";
    case Family::Debian13: return "+debian13_amd64.deb";
    case Family::Ubuntu: return "+ubuntu26.04_amd64.deb";
    default: return nullptr;
    }
}
