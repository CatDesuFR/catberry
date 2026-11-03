#include "releases.h"

#include "version.h"

#include <cctype>
#include <sstream>

std::vector<std::string> toolVersions(const std::string &json, const std::string &tool) {
    const std::string key = "\"tag_name\"";
    const std::string prefix = tool + "-v";
    std::vector<std::string> found;
    for (size_t at = json.find(key); at != std::string::npos; at = json.find(key, at + key.size())) {
        // A real key, not text inside another string (that would be \"tag_name\").
        if (at > 0 && json[at - 1] == '\\') continue;
        size_t p = at + key.size();
        while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
        if (p >= json.size() || json[p] != ':') continue;
        ++p;
        while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
        if (p >= json.size() || json[p] != '"') continue;
        size_t end = json.find('"', p + 1);
        if (end == std::string::npos) break;
        std::string tag = json.substr(p + 1, end - p - 1);
        if (tag.compare(0, prefix.size(), prefix) == 0) found.push_back(tag.substr(prefix.size()));
    }
    return sortNewestFirst(found);
}

bool findAsset(const std::string &sums, const std::string &program, const std::string &suffix, SumEntry &out) {
    std::istringstream lines(sums);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // "<hash>  <name>" (or "<hash> *<name>" in binary mode).
        size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        std::string hash = line.substr(0, space);
        size_t n = line.find_first_not_of(" *", space);
        if (n == std::string::npos) continue;
        std::string name = line.substr(n);
        bool ours;
        if (suffix == "-windows.zip") {
            ours = name == program + suffix;
        } else {
            ours = name.size() > program.size() + 1 + suffix.size() && name.compare(0, program.size(), program) == 0 &&
                   (name[program.size()] == '-' || name[program.size()] == '_') &&
                   std::isdigit(static_cast<unsigned char>(name[program.size() + 1])) &&
                   name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
        }
        if (ours) {
            out = {hash, name};
            return true;
        }
    }
    return false;
}
