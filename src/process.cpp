#include "process.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>

namespace {
// Windows passes one command line; quote arguments the way the C runtime
// splits them again.
std::string quoteArg(const std::string &arg) {
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

std::string commandLine(const std::vector<std::string> &argv) {
    std::string line;
    for (const std::string &a : argv) line += (line.empty() ? "" : " ") + quoteArg(a);
    return line;
}
} // namespace

int runProgram(const std::vector<std::string> &argv) {
    std::string line = commandLine(argv);
    STARTUPINFOA startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION info{};
    if (!CreateProcessA(nullptr, &line[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &info)) return 127;
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);
    return static_cast<int>(code);
}

bool captureOutput(const std::vector<std::string> &argv, std::string &out) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE readEnd, writeEnd;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) return false;
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    std::string line = commandLine(argv);
    STARTUPINFOA startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writeEnd;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION info{};
    BOOL started = CreateProcessA(nullptr, &line[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                  &startup, &info);
    CloseHandle(writeEnd);
    out.clear();
    if (!started) {
        CloseHandle(readEnd);
        return false;
    }
    char buf[4096];
    DWORD n;
    while (ReadFile(readEnd, buf, sizeof buf, &n, nullptr) && n > 0) out.append(buf, n);
    CloseHandle(readEnd);
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);
    return code == 0;
}

bool isRoot() { return false; }
std::vector<std::string> withSudo(std::vector<std::string> argv) { return argv; }

#else

#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>

namespace {
std::vector<char *> cArgs(const std::vector<std::string> &argv) {
    std::vector<char *> args;
    for (const std::string &a : argv) args.push_back(const_cast<char *>(a.c_str()));
    args.push_back(nullptr);
    return args;
}

int waitFor(pid_t pid) {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return 127;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}
} // namespace

int runProgram(const std::vector<std::string> &argv) {
    if (argv.empty()) return 127;
    std::vector<char *> args = cArgs(argv);
    pid_t pid = fork();
    if (pid < 0) return 127;
    if (pid == 0) {
        execvp(args[0], args.data());
        _exit(127);
    }
    return waitFor(pid);
}

bool captureOutput(const std::vector<std::string> &argv, std::string &out) {
    out.clear();
    if (argv.empty()) return false;
    int fds[2];
    if (pipe(fds) != 0) return false;
    std::vector<char *> args = cArgs(argv);
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]);
        close(fds[1]);
        execvp(args[0], args.data());
        _exit(127);
    }
    close(fds[1]);
    char buf[4096];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR))
        if (n > 0) out.append(buf, size_t(n));
    close(fds[0]);
    return waitFor(pid) == 0;
}

bool isRoot() { return geteuid() == 0; }

std::vector<std::string> withSudo(std::vector<std::string> argv) {
    if (!isRoot()) argv.insert(argv.begin(), "sudo");
    return argv;
}

#endif
