#include "fetch.h"

#include "process.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

TempDir::TempDir() {
#ifdef _WIN32
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec);
    for (int i = 0; i < 100 && path_.empty(); ++i) {
        fs::path p = base / ("catberry-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount()) +
                             "-" + std::to_string(i));
        if (fs::create_directory(p, ec)) path_ = p.string();
    }
#else
    const char *tmp = std::getenv("TMPDIR");
    std::string pattern = std::string(tmp && *tmp ? tmp : "/tmp") + "/catberry-XXXXXX";
    std::vector<char> buf(pattern.begin(), pattern.end());
    buf.push_back('\0');
    if (mkdtemp(buf.data())) path_ = buf.data();
#endif
}

TempDir::~TempDir() {
    if (path_.empty()) return;
    std::error_code ec;
    fs::remove_all(path_, ec);
}

bool download(const std::string &url, const std::string &file) {
#ifdef _WIN32
    const char *curl = "curl.exe";
#else
    const char *curl = "curl";
#endif
    return runProgram({curl, "-fsSL", "--retry", "2", "-o", file, url}) == 0;
}

namespace {
std::string lowerHex(const std::string &s) {
    std::string out;
    for (char c : s)
        if (std::isxdigit(static_cast<unsigned char>(c))) out += char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
} // namespace

bool sha256Matches(const std::string &file, const std::string &expected) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return false;
    std::string out;
#ifdef _WIN32
    // certutil prints a header line, the hash (older versions with spaces), and a footer.
    if (!captureOutput({"certutil", "-hashfile", file, "SHA256"}, out)) return false;
    size_t first = out.find('\n');
    if (first == std::string::npos) return false;
    size_t second = out.find('\n', first + 1);
    std::string hash = lowerHex(out.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1));
#else
    if (!captureOutput({"sha256sum", "--", file}, out)) return false;
    std::string hash = lowerHex(out.substr(0, out.find(' ')));
#endif
    return hash.size() == 64 && hash == lowerHex(expected);
}
