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

#include "args.h"
#include "manage.h"
#include "tools.h"

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


#else


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
        if (std::string(tool.name) == "catberry") continue;
        std::string path = findTool(tool.program);
        std::string version = path.empty() ? "" : toolVersion(path);
        std::printf("  %-7s %s\n", tool.name, path.empty() ? "not installed" : version.empty() ? "?" : version.c_str());
    }
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    Command command = parseCommand(argc, argv);
    switch (command.kind) {
    case Command::Help:
        usage(stdout);
        return 0;
    case Command::Versions_:
        return printVersions();
    case Command::Install:
        return installTool(command.tool, command.version, command.reinstall);
    case Command::Uninstall:
        return uninstallTool(command.tool);
    case Command::ListVersions:
        return listVersions(command.tool);
    case Command::RunTool:
        for (const Tool &tool : kTools)
            if (command.tool == tool.name)
                return dispatch(tool, argc - command.toolArgStart, argv + command.toolArgStart);
        break;
    default:
        break;
    }

    if (argc > 1) std::fprintf(stderr, "catberry: unknown command '%s'\n\n", argv[1]);
    usage(stderr);
    return 2;
}
