#include "../src/version.h"
#include "test.h"

namespace {
int cmp(const char *a, const char *b) {
    Version va, vb;
    if (!parseVersion(a, va) || !parseVersion(b, vb)) return 99;
    return compareVersions(va, vb);
}
} // namespace

void testVersion() {
    CHECK(cmp("1.10.0", "1.9.2") == 1);
    CHECK(cmp("1.9.2", "1.10.0") == -1);
    CHECK(cmp("1.2", "1.2.0") == 0);
    Version v;
    CHECK(parseVersion("v1.2.2", v) && versionString(v) == "1.2.2");
    CHECK(!parseVersion("", v));
    CHECK(!parseVersion("1.a", v));
    CHECK(!parseVersion("1..2", v));
    CHECK(!parseVersion("1.2.", v));
    CHECK((sortNewestFirst({"1.1.0", "1.10.0", "1.9.2", "x"}) == std::vector<std::string>{"1.10.0", "1.9.2", "1.1.0"}));
}
