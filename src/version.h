// Version numbers like 1.2.2, compared number by number (1.10.0 > 1.9.2).
#pragma once

#include <string>
#include <vector>

struct Version {
    std::vector<int> parts;
};

// Accepts "1.2.2" and "v1.2.2"; false for anything else.
bool parseVersion(const std::string &text, Version &out);
// -1, 0 or 1. Missing parts count as 0, so 1.2 == 1.2.0.
int compareVersions(const Version &a, const Version &b);
std::string versionString(const Version &v);
// Newest first; strings that are not versions are dropped.
std::vector<std::string> sortNewestFirst(std::vector<std::string> versions);
