#include "args.h"

#include <vector>

namespace {

bool isManaged(const std::string &name) { return isToolName(name) || name == "catberry"; }

// The words after "install <tool>": an optional version and --reinstall.
bool parseInstallRest(const std::vector<std::string> &rest, Command &c) {
    c.version = "latest";
    bool haveVersion = false;
    for (const std::string &w : rest) {
        if (w == "--reinstall") {
            c.reinstall = true;
        } else if (!haveVersion && !w.empty() && w[0] != '-') {
            c.version = w;
            haveVersion = true;
        } else {
            return false;
        }
    }
    return true;
}

Command manage(const std::string &verb, const std::string &tool, const std::vector<std::string> &rest) {
    Command c;
    if (!isManaged(tool)) return c;
    c.tool = tool;
    if (verb == "install") {
        c.kind = parseInstallRest(rest, c) ? Command::Install : Command::Unknown;
    } else if (verb == "update") {
        c.kind = rest.empty() ? Command::Update : Command::Unknown;
    } else {
        c.kind = rest.empty() ? (verb == "uninstall" ? Command::Uninstall : Command::ListVersions) : Command::Unknown;
    }
    return c;
}

bool isVerb(const std::string &w) { return w == "install" || w == "uninstall" || w == "versions" || w == "update"; }

} // namespace

bool isToolName(const std::string &name) { return name == "ac" || name == "mtools" || name == "roofce" || name == "cbmm"; }

Command parseCommand(int argc, char **argv) {
    std::vector<std::string> a(argv + 1, argv + argc);
    Command c;
    if (a.empty()) return c;
    const std::string &first = a[0];
    if (first == "-help" || first == "-h" || first == "--help") {
        c.kind = Command::Help;
    } else if (first == "-v" || first == "--version") {
        c.kind = Command::Versions_;
    } else if (first == "update" && a.size() == 1) {
        c.kind = Command::Update; // everything installed
    } else if (isVerb(first)) {
        if (a.size() >= 2) c = manage(first, a[1], std::vector<std::string>(a.begin() + 2, a.end()));
    } else if (isManaged(first) && a.size() >= 2 && isVerb(a[1])) {
        c = manage(a[1], first, std::vector<std::string>(a.begin() + 2, a.end()));
    } else if (isToolName(first)) {
        c.kind = Command::RunTool;
        c.tool = first;
        c.toolArgStart = 2;
    } else if (first == "-config" || first == "-configload" || first == "-configunload" || first == "-quickpick" ||
               first == "-quickload" || first == "-quickunload") {
        // The hotkey macro and quick click commands belong to mtools but keep their short form.
        c.kind = Command::RunTool;
        c.tool = "mtools";
        c.toolArgStart = 1;
    }
    return c;
}
