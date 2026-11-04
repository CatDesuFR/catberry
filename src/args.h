// What the command line asks for. install / uninstall / versions work in
// both orders ("catberry install ac" and "catberry ac install"); anything
// else after a tool name is passed to that tool untouched.
#pragma once

#include <string>

struct Command {
    enum Kind { Help, Versions_, RunTool, Install, Uninstall, ListVersions, Update, Unknown } kind = Unknown;
    std::string tool;    // ac, mtools, roofce or catberry ("" for update: everything)
    std::string version; // Install only: a version or "latest"
    bool reinstall = false;
    int toolArgStart = 0; // RunTool: the tool's arguments are argv[toolArgStart..]
};

bool isToolName(const std::string &name); // ac, mtools, roofce
Command parseCommand(int argc, char **argv);
