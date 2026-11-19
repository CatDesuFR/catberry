// The catberry programs: their names, where they are installed, and
// which version they are.
#pragma once

#include <string>

extern const char *const kVersion; // this launcher's version
extern const char *const kRepo;

struct Tool {
    const char *name;    // ac, mtools, roofce, cbmm, vskm, catberry
    const char *program; // catberry-ac, ..., catberry
    const char *branch;
};
extern const Tool kTools[6]; // ac, mtools, roofce, cbmm, vskm, catberry (the launcher last)

const Tool *toolByName(const std::string &name);
// Path of the installed program: next to this launcher first, then on
// PATH. "" when not installed.
std::string findTool(const char *program);
// The version the program reports with -v ("catberry roofce 1.2.2" gives
// "1.2.2"); "" if it does not answer.
std::string toolVersion(const std::string &path);
