// Runs every test; exit code 1 if any check failed.
#include "test.h"

int g_checks = 0, g_failures = 0;

int main() {
    testVersion();
    testReleases();
    testSystem();
    testArgs();
    testFetch();
    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}
