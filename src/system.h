// Which Linux system this is, from /etc/os-release, and so which package
// file of a release fits it.
#pragma once

#include <string>

enum class Family { Arch, Fedora, Debian13, Ubuntu, Unsupported };

struct SystemInfo {
    Family family = Family::Unsupported;
    std::string prettyName; // PRETTY_NAME, for messages
};

// Arch-based: ID or ID_LIKE has "arch". Fedora-based: "fedora". Debian 13.
// Ubuntu 26.04 or newer, and distros built on it (by UBUNTU_CODENAME).
SystemInfo detectSystem(const std::string &osRelease);
// The end of the matching file's name, or nullptr when unsupported.
const char *assetSuffix(Family family);
