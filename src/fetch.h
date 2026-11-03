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

// Downloads `url` to `file` with curl. `status` is the server's HTTP answer
// (200, 404, ...), or 0 when there was none (no connection, file:// URLs).
// True only for a complete download with a 2xx answer (or none, for file://).
bool fetchUrl(const std::string &url, const std::string &file, int &status);
// The same, when the reason does not matter.
bool download(const std::string &url, const std::string &file);
// True when the file's SHA-256 is `expected` (any letter case).
bool sha256Matches(const std::string &file, const std::string &expected);
