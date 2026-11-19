#include "tools.h"

#include "process.h"

#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

const char *const kVersion = "1.3.7";
const char *const kRepo = "https://github.com/CatDesuFR/catberry";

const Tool kTools[6] = {{"ac", "catberry-ac", "ac"},
                        {"mtools", "catberry-mtools", "mtools"},
                        {"roofce", "catberry-roofce", "roof-carton-engine"},
                        {"cbmm", "catberry-cbmm", "cbmm"},
                        {"vskm", "catberry-vskm", "vskm"},
                        {"catberry", "catberry", "main"}};

const Tool *toolByName(const std::string &name) {
    for (const Tool &tool : kTools)
        if (name == tool.name) return &tool;
    return nullptr;
}

#ifdef _WIN32

std::string findTool(const char *program) {
    std::string exe = std::string(program) + ".exe";
    char self[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string dir(self, n);
    dir = dir.substr(0, dir.find_last_of("\\/") + 1);
    std::string candidate = dir + exe;
    if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) return candidate;
    char found[MAX_PATH];
    if (SearchPathA(nullptr, exe.c_str(), nullptr, MAX_PATH, found, nullptr)) return found;
    return "";
}

#else

std::string findTool(const char *program) {
    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n > 0) {
        std::string dir(self, n);
        std::string candidate = dir.substr(0, dir.rfind('/') + 1) + program;
        if (access(candidate.c_str(), X_OK) == 0) return candidate;
    }
    const char *path = std::getenv("PATH");
    std::string dirs = path ? path : "/usr/local/bin:/usr/bin";
    size_t start = 0;
    while (start <= dirs.size()) {
        size_t end = dirs.find(':', start);
        if (end == std::string::npos) end = dirs.size();
        std::string candidate = dirs.substr(start, end - start);
        candidate = (candidate.empty() ? "." : candidate) + "/" + program;
        if (access(candidate.c_str(), X_OK) == 0) return candidate;
        start = end + 1;
    }
    return "";
}

#endif

std::string toolVersion(const std::string &path) {
    std::string out;
    captureOutput({path, "-v"}, out);
    std::string line = out.substr(0, out.find('\n'));
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    size_t space = line.find_last_of(' ');
    return space == std::string::npos ? line : line.substr(space + 1);
}
