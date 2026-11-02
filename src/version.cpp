#include "version.h"

#include <algorithm>
#include <cctype>

bool parseVersion(const std::string &text, Version &out) {
    std::string s = !text.empty() && (text[0] == 'v' || text[0] == 'V') ? text.substr(1) : text;
    if (s.empty()) return false;
    Version v;
    size_t start = 0;
    while (true) {
        size_t dot = s.find('.', start);
        std::string part = s.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (part.empty() || part.size() > 9) return false;
        for (char c : part)
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        v.parts.push_back(std::stoi(part));
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    out = v;
    return true;
}

int compareVersions(const Version &a, const Version &b) {
    size_t n = std::max(a.parts.size(), b.parts.size());
    for (size_t i = 0; i < n; ++i) {
        int x = i < a.parts.size() ? a.parts[i] : 0;
        int y = i < b.parts.size() ? b.parts[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

std::string versionString(const Version &v) {
    std::string out;
    for (size_t i = 0; i < v.parts.size(); ++i) out += (i ? "." : "") + std::to_string(v.parts[i]);
    return out;
}

std::vector<std::string> sortNewestFirst(std::vector<std::string> versions) {
    std::vector<Version> parsed;
    for (const std::string &s : versions) {
        Version v;
        if (parseVersion(s, v)) parsed.push_back(v);
    }
    std::sort(parsed.begin(), parsed.end(),
              [](const Version &a, const Version &b) { return compareVersions(a, b) > 0; });
    std::vector<std::string> out;
    for (const Version &v : parsed) out.push_back(versionString(v));
    return out;
}
