// Runs every test in a throwaway config folder; exit code 1 on any failure.
#include "test.h"

#include <cstdlib>
#include <string>
#include <unistd.h>

int g_checks = 0, g_failures = 0;

int main() {
    char dir[] = "/tmp/catberry-mtools-test-XXXXXX";
    if (!mkdtemp(dir)) return 2;
    setenv("XDG_CONFIG_HOME", dir, 1);
    testMacro();
    std::system(("rm -rf '" + std::string(dir) + "'").c_str());
    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}
