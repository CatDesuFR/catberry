// Running other programs (curl, the package manager, the tools).
#pragma once

#include <string>
#include <vector>

// Runs a program with this terminal, waits, returns its exit code
// (127 when it cannot be started).
int runProgram(const std::vector<std::string> &argv);
// Runs a program and collects what it prints; true when it exits with 0.
bool captureOutput(const std::vector<std::string> &argv, std::string &out);
bool isRoot();
// The command prefixed with sudo, unless already root (Linux); unchanged on Windows.
std::vector<std::string> withSudo(std::vector<std::string> argv);
