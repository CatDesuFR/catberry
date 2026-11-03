// Downloads into a temporary folder, and checking them against SHA256SUMS.
#pragma once

#include <string>

// A new, empty folder that is removed, with everything in it, when this
// goes away.
class TempDir {
public:
    TempDir();
    ~TempDir();
    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;
    const std::string &path() const { return path_; }

private:
    std::string path_;
};

// Downloads `url` to `file` with curl; false on any failure.
bool download(const std::string &url, const std::string &file);
// True when the file's SHA-256 is `expected` (any letter case).
bool sha256Matches(const std::string &file, const std::string &expected);
