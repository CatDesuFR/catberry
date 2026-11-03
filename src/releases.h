// What GitHub tells us about the releases: the versions of a tool, and
// the files of one release with their SHA-256 hashes.
#pragma once

#include <string>
#include <vector>

// From GitHub's release list (JSON), the versions of `tool`: every
// "tag_name" of the form "<tool>-v<version>", newest first.
std::vector<std::string> toolVersions(const std::string &releasesJson, const std::string &tool);

struct SumEntry {
    std::string sha256, name;
};

// The file of `program` ending in `suffix`, from a release's SHA256SUMS.
// A file belongs to a program when its name is the program, then '-' or
// '_', then a digit (so catberry-ac-... is not the launcher's). The
// Windows zip is exactly "<program>-windows.zip".
bool findAsset(const std::string &sums, const std::string &program, const std::string &suffix, SumEntry &out);
