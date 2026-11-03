#include "../src/args.h"
#include "test.h"

#include <vector>

namespace {
Command parse(std::vector<const char *> words) {
    std::vector<char *> argv{const_cast<char *>("catberry")};
    for (const char *w : words) argv.push_back(const_cast<char *>(w));
    return parseCommand(int(argv.size()), argv.data());
}
} // namespace

void testArgs() {
    Command c = parse({"install", "roofce"});
    CHECK(c.kind == Command::Install && c.tool == "roofce" && c.version == "latest" && !c.reinstall);
    c = parse({"roofce", "install", "1.2.1"});
    CHECK(c.kind == Command::Install && c.tool == "roofce" && c.version == "1.2.1");
    c = parse({"install", "ac", "--reinstall"});
    CHECK(c.kind == Command::Install && c.tool == "ac" && c.version == "latest" && c.reinstall);
    c = parse({"ac", "install", "--reinstall", "v1.1.2"});
    CHECK(c.kind == Command::Install && c.version == "v1.1.2" && c.reinstall);
    CHECK(parse({"ac", "uninstall"}).kind == Command::Uninstall);
    c = parse({"uninstall", "ac"});
    CHECK(c.kind == Command::Uninstall && c.tool == "ac");
    c = parse({"versions", "mtools"});
    CHECK(c.kind == Command::ListVersions && c.tool == "mtools");
    CHECK(parse({"mtools", "versions"}).kind == Command::ListVersions);
    c = parse({"install", "catberry"});
    CHECK(c.kind == Command::Install && c.tool == "catberry");
    CHECK(parse({"catberry", "uninstall"}).kind == Command::Uninstall);

    // Everything else still goes to the tool, untouched.
    c = parse({"roofce", "gui"});
    CHECK(c.kind == Command::RunTool && c.tool == "roofce" && c.toolArgStart == 2);
    c = parse({"roofce"});
    CHECK(c.kind == Command::RunTool && c.tool == "roofce" && c.toolArgStart == 2);
    c = parse({"-configload", "X"});
    CHECK(c.kind == Command::RunTool && c.tool == "mtools" && c.toolArgStart == 1);

    CHECK(parse({"install", "nope"}).kind == Command::Unknown);
    CHECK(parse({"install"}).kind == Command::Unknown);
    CHECK(parse({"install", "ac", "1.0", "2.0"}).kind == Command::Unknown);
    CHECK(parse({"bogus"}).kind == Command::Unknown);
    CHECK(parse({}).kind == Command::Unknown);
    CHECK(parse({"-h"}).kind == Command::Help);
    CHECK(parse({"-v"}).kind == Command::Versions_);
}
