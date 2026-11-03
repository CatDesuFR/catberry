#include "../src/fetch.h"
#include "../src/process.h"
#include "test.h"

#include <fstream>
#include <sys/stat.h>

namespace {
bool exists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}
} // namespace

void testFetch() {
    std::string dirPath;
    {
        TempDir dir;
        dirPath = dir.path();
        CHECK(!dirPath.empty() && exists(dirPath));
        std::string file = dir.path() + "/abc.txt";
        std::ofstream(file) << "abc";
        CHECK(sha256Matches(file, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        CHECK(sha256Matches(file, "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));
        CHECK(!sha256Matches(file, "0000000000000000000000000000000000000000000000000000000000000000"));
        CHECK(!sha256Matches(dir.path() + "/missing", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        // A file:// URL goes through the same curl path as https.
        CHECK(download("file://" + file, dir.path() + "/copy.txt"));
        CHECK(sha256Matches(dir.path() + "/copy.txt", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        CHECK(!download("file://" + dir.path() + "/missing", dir.path() + "/x"));
        // The server's answer is kept: 404 means "not there", not "no internet".
        int status = -1;
        CHECK(fetchUrl("file://" + file, dir.path() + "/copy2.txt", status) && status == 0);
        CHECK(!fetchUrl("http://127.0.0.1:9/nothing", dir.path() + "/y", status) && status == 0);
        std::ofstream(dir.path() + "/sub-file") << "x";
    }
    CHECK(!exists(dirPath)); // removed with everything in it

    std::string out;
    CHECK(captureOutput({"echo", "hi"}, out) && out == "hi\n");
    CHECK(!captureOutput({"false"}, out));
    CHECK(runProgram({"true"}) == 0);
    CHECK(runProgram({"false"}) == 1);
    CHECK(runProgram({"no-such-program-xyz"}) == 127);
    CHECK(withSudo({"pacman", "-U", "x"}).size() == (isRoot() ? 3u : 4u));
}
