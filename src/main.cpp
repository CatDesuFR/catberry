// catberry - the launcher for the catberry tools. Each tool is its own
// program, installed separately next to this one:
//   catberry ac ...                     runs catberry-ac ...
//   catberry mtools ...                 runs catberry-mtools ...
//   catberry -config/-configload/...    runs catberry-mtools -config/...

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace {

const char *const kVersion = "1.2.0";
const char *const kRepo = "https://github.com/CatDesuFR/catberry";

struct Tool {
    const char *name;
    const char *program;
    const char *branch;
};
const Tool kTools[] = {
    {"ac", "catberry-ac", "ac"}, {"mtools", "catberry-mtools", "mtools"}, {"roofce", "catberry-roofce", "roof-carton-engine"}};

void usage(FILE *out) {
    std::fprintf(out,
                 "catberry %s\n"
                 "\n"
                 "usage:\n"
                 "  catberry -help                  show this help\n"
                 "  catberry -v                     print the versions of catberry and its tools\n"
                 "\n"
                 "  catberry ac gui                 open the autoclicker\n"
                 "  catberry ac -v                  print the autoclicker version\n"
                 "  catberry mtools gui             open the macro recorder\n"
                 "  catberry mtools -v              print the macro recorder version\n"
                 "  catberry -config NAME KEYS HOTKEY COUNT\n"
                 "                                  save a hotkey macro, eg: -config EasyEdit E,LMB F6 1\n"
                 "  catberry -configload NAME       start a saved macro in the background\n"
                 "  catberry -configunload [NAME]   stop one loaded macro, or all of them\n"
                 "  catberry roofce gui             open the wallpaper manager (Linux, Wayland)\n"
                 "  catberry roofce -v              print the wallpaper manager version\n"
                 "\n"
                 "ac, mtools and roofce are installed separately: %s\n",
                 kVersion, kRepo);
}

#ifdef _WIN32

std::string exeName(const char *program) { return std::string(program) + ".exe"; }

// Next to this launcher first, then on PATH.
std::string findTool(const char *program) {
    char self[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string dir(self, n);
    dir = dir.substr(0, dir.find_last_of("\\/") + 1);
    std::string candidate = dir + exeName(program);
    if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) return candidate;
    char found[MAX_PATH];
    if (SearchPathA(nullptr, exeName(program).c_str(), nullptr, MAX_PATH, found, nullptr)) return found;
    return "";
}

// Windows passes one command line; quote arguments the way the C runtime
// splits them again.
std::string quote(const std::string &arg) {
    if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos) return arg;
    std::string out = "\"";
    size_t slashes = 0;
    for (char c : arg) {
        if (c == '\\') {
            ++slashes;
            continue;
        }
        out.append(c == '"' ? slashes * 2 + 1 : slashes, '\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, '\\');
    return out + "\"";
}

// Runs the tool and waits for it, so the terminal behaves as if the tool
// had been started directly.
int runTool(const std::string &path, int argc, char **argv) {
    std::string command = quote(path);
    for (int i = 0; i < argc; ++i) command += " " + quote(argv[i]);
    STARTUPINFOA startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION info{};
    if (!CreateProcessA(path.c_str(), &command[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &info)) {
        std::fprintf(stderr, "catberry: cannot start %s\n", path.c_str());
        return 1;
    }
    // Ctrl+C reaches the tool through the shared console; the launcher
    // just keeps waiting for it.
    SetConsoleCtrlHandler(nullptr, TRUE);
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);
    return static_cast<int>(code);
}

std::string toolVersion(const std::string &path) {
    // cmd strips the outer quotes, leaving the quoted path intact.
    std::string command = "\"\"" + path + "\" -v\"";
    FILE *pipe = _popen(command.c_str(), "r");
    if (!pipe) return "";
    char line[128] = "";
    if (!std::fgets(line, sizeof line, pipe)) line[0] = '\0';
    _pclose(pipe);
    return line;
}

#else

// Next to this launcher first, then on PATH.
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

// Replaces this process with the tool, so signals, exit codes and the
// background processes of -configload work exactly as if it was run directly.
int runTool(const std::string &path, int argc, char **argv) {
    std::string name = path.substr(path.rfind('/') + 1);
    char **args = new char *[argc + 2];
    args[0] = &name[0];
    for (int i = 0; i < argc; ++i) args[i + 1] = argv[i];
    args[argc + 1] = nullptr;
    execv(path.c_str(), args);
    std::fprintf(stderr, "catberry: cannot start %s: %s\n", path.c_str(), std::strerror(errno));
    return 1;
}

std::string toolVersion(const std::string &path) {
    std::string command = "'" + path + "' -v 2>/dev/null";
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe) return "";
    char line[128] = "";
    if (!std::fgets(line, sizeof line, pipe)) line[0] = '\0';
    pclose(pipe);
    return line;
}

#endif

int dispatch(const Tool &tool, int argc, char **argv) {
    std::string path = findTool(tool.program);
    if (path.empty()) {
        std::fprintf(stderr,
                     "catberry: %s is not installed.\n"
                     "Get it from the '%s' branch: %s/tree/%s\n",
                     tool.name, tool.branch, kRepo, tool.branch);
        return 1;
    }
    return runTool(path, argc, argv);
}

int printVersions() {
    std::printf("catberry %s\n", kVersion);
    for (const Tool &tool : kTools) {
        std::string path = findTool(tool.program);
        std::string version = path.empty() ? "" : toolVersion(path);
        // Tools answer "catberry <name> <version>".
        size_t space = version.find_last_of(' ');
        if (space != std::string::npos) version = version.substr(space + 1);
        while (!version.empty() && (version.back() == '\n' || version.back() == '\r')) version.pop_back();
        std::printf("  %-7s %s\n", tool.name, path.empty() ? "not installed" : version.empty() ? "?" : version.c_str());
    }
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    std::string command = argc > 1 ? argv[1] : "";

    if (command == "-help" || command == "-h" || command == "--help") {
        usage(stdout);
        return 0;
    }
    if (command == "-v" || command == "--version") return printVersions();
    for (const Tool &tool : kTools)
        if (command == tool.name) return dispatch(tool, argc - 2, argv + 2);
    // The hotkey macro commands belong to mtools but keep their short form.
    if (command == "-config" || command == "-configload" || command == "-configunload")
        return dispatch(kTools[1], argc - 1, argv + 1);

    if (!command.empty()) std::fprintf(stderr, "catberry: unknown command '%s'\n\n", command.c_str());
    usage(stderr);
    return 2;
}
