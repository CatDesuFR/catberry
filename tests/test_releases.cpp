#include "../src/releases.h"
#include "test.h"

#include <fstream>
#include <sstream>

namespace {
std::string readFile(const char *path) {
    std::ifstream in(path);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}
using List = std::vector<std::string>;

const char *kSums =
    "aaa  catberry-1.2.0-1-x86_64.pkg.tar.zst\n"
    "bbb  catberry-ac-1.1.2-1-x86_64.pkg.tar.zst\n"
    "ccc  catberry_1.2.0-1+debian13_amd64.deb\n"
    "ddd  catberry-ac_1.1.2-1+debian13_amd64.deb\n"
    "eee  catberry-windows.zip\n"
    "fff  catberry-ac-windows.zip\n";
} // namespace

void testReleases() {
    std::string json = readFile("data/releases.json");
    CHECK(!json.empty());
    // Only real tag_name fields; escaped text in a body does not count.
    CHECK((toolVersions(json, "catberry") == List{"1.10.0", "1.2.0"}));
    CHECK((toolVersions(json, "ac") == List{"1.1.2"}));
    CHECK((toolVersions(json, "roofce") == List{"1.2.2"}));
    CHECK((toolVersions(json, "mtools") == List{"1.1.0"}));
    CHECK(toolVersions("[]", "ac").empty());

    SumEntry e;
    CHECK(findAsset(kSums, "catberry", ".pkg.tar.zst", e) && e.name == "catberry-1.2.0-1-x86_64.pkg.tar.zst" &&
          e.sha256 == "aaa");
    CHECK(findAsset(kSums, "catberry-ac", ".pkg.tar.zst", e) && e.sha256 == "bbb");
    CHECK(findAsset(kSums, "catberry", "+debian13_amd64.deb", e) && e.sha256 == "ccc");
    CHECK(findAsset(kSums, "catberry", "-windows.zip", e) && e.name == "catberry-windows.zip");
    CHECK(findAsset(kSums, "catberry-ac", "-windows.zip", e) && e.sha256 == "fff");
    CHECK(!findAsset(kSums, "catberry", ".rpm", e));
    CHECK(!findAsset(kSums, "catberry-mtools", ".pkg.tar.zst", e));
}
