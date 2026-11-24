// catberry-cbmm: installs Catberry's Mod Menu into Geometry Dash.
//
// The compiled mod (catdesu.cats-mod-menu.geode) is built into this program,
// so it is one file on every system:
//   catberry cbmm load      copy the mod into Geometry Dash's geode/mods folder
//   catberry cbmm unload    remove it from there again
//   catberry cbmm status    show where Geometry Dash is and whether the mod is loaded
//   catberry cbmm -v        print the version
//
// Geometry Dash is found in the Steam libraries (Linux, including Proton, and
// Windows). Geode must already be installed in it.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

#ifndef CBMM_VERSION
#define CBMM_VERSION "0.0.0"
#endif
#ifndef CBMM_GEODE
#error "build with -DCBMM_GEODE=\"path/to/catdesu.cats-mod-menu.geode\""
#endif
#ifndef CBMM_GEODE_2081
#error "build with -DCBMM_GEODE_2081=\"path/to/the GD 2.2081 build of the mod\""
#endif

// The mod file, embedded at build time.
#ifdef _WIN32
#define CBMM_SECTION ".rdata,\"dr\""
#else
#define CBMM_SECTION ".rodata"
#endif
__asm__(".section " CBMM_SECTION "\n"
        ".global cbmm_geode_start\n"
        ".balign 16\n"
        "cbmm_geode_start:\n"
        ".incbin \"" CBMM_GEODE "\"\n"
        ".global cbmm_geode_end\n"
        "cbmm_geode_end:\n"
        ".byte 0\n"
        ".balign 16\n"
        ".global cbmm_geode2081_start\n"
        "cbmm_geode2081_start:\n"
        ".incbin \"" CBMM_GEODE_2081 "\"\n"
        ".global cbmm_geode2081_end\n"
        "cbmm_geode2081_end:\n"
        ".byte 0\n"
        ".text\n");  // back to code (MinGW's assembler has no .previous)
extern "C" const unsigned char cbmm_geode_start[];
extern "C" const unsigned char cbmm_geode_end[];
extern "C" const unsigned char cbmm_geode2081_start[];
extern "C" const unsigned char cbmm_geode2081_end[];

namespace {

const char *const kModFile = "catdesu.cats-mod-menu.geode";
const char *const kModName = "Catberry's Mod Menu";

// The mod is built twice: for GD 2.2091 (Geode 6) and GD 2.2081 (Geode 5).
struct Build {
    const char *gd;
    const char *geode;
    const unsigned char *start, *end;
    size_t size() const { return static_cast<size_t>(end - start); }
};
const Build kBuilds[] = {
    {"2.2091", "6", cbmm_geode_start, cbmm_geode_end},
    {"2.2081", "5", cbmm_geode2081_start, cbmm_geode2081_end},
};

void usage(FILE *out) {
    std::fprintf(out,
                 "catberry cbmm %s - %s for Geometry Dash (Geode)\n"
                 "\n"
                 "usage:\n"
                 "  catberry cbmm load      copy the mod into Geometry Dash's geode/mods folder\n"
                 "  catberry cbmm unload    remove the mod from there\n"
                 "  catberry cbmm status    show where Geometry Dash is and whether the mod is loaded\n"
                 "  catberry cbmm -v        print the version\n"
                 "\n"
                 "Geometry Dash is found in your Steam libraries. Geode must be installed in it\n"
                 "(https://geode-sdk.org). The mod is built for GD 2.2091 (Geode 6) and 2.2081\n"
                 "(Geode 5): load picks the one for your game. Restart it after load or unload.\n",
                 CBMM_VERSION, kModName);
}

std::string readText(const fs::path &file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The "path" entries of Steam's steamapps/libraryfolders.vdf.
std::vector<fs::path> libraryFolders(const fs::path &steam) {
    std::vector<fs::path> out{steam};
    std::string text = readText(steam / "steamapps" / "libraryfolders.vdf");
    size_t at = 0;
    while ((at = text.find("\"path\"", at)) != std::string::npos) {
        size_t open = text.find('"', at + 6);
        if (open == std::string::npos) break;
        std::string value;
        size_t i = open + 1;
        for (; i < text.size() && text[i] != '"'; ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) ++i;  // vdf escapes: \\ -> '\'
            value += text[i];
        }
        out.emplace_back(value);
        at = i + 1;
    }
    return out;
}

std::vector<fs::path> steamRoots() {
    std::vector<fs::path> roots;
#ifdef _WIN32
    char buffer[MAX_PATH];
    DWORD size = sizeof buffer;
    if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", RRF_RT_REG_SZ, nullptr, buffer, &size) ==
        ERROR_SUCCESS)
        roots.emplace_back(buffer);
    roots.emplace_back("C:\\Program Files (x86)\\Steam");
    roots.emplace_back("C:\\Program Files\\Steam");
#else
    const char *home = std::getenv("HOME");
    std::string h = home ? home : "";
    const char *dataHome = std::getenv("XDG_DATA_HOME");
    if (dataHome && *dataHome) roots.emplace_back(fs::path(dataHome) / "Steam");
    roots.emplace_back(h + "/.local/share/Steam");
    roots.emplace_back(h + "/.steam/steam");
    roots.emplace_back(h + "/.steam/root");
    roots.emplace_back(h + "/.var/app/com.valvesoftware.Steam/.local/share/Steam");
    roots.emplace_back(h + "/snap/steam/common/.local/share/Steam");
#endif
    return roots;
}

// The Geometry Dash folder, or "" if it is not in any Steam library.
fs::path findGeometryDash() {
    std::error_code ec;
    // CATBERRY_CBMM_GD: another Geometry Dash folder (for tests).
    if (const char *dir = std::getenv("CATBERRY_CBMM_GD"); dir && *dir) return fs::path(dir);
    for (const auto &root : steamRoots()) {
        if (!fs::exists(root, ec)) continue;
        for (const auto &lib : libraryFolders(root)) {
            fs::path gd = lib / "steamapps" / "common" / "Geometry Dash";
            if (fs::exists(gd / "GeometryDash.exe", ec)) return gd;
        }
    }
    return {};
}

// Finds Geometry Dash and its geode/mods folder; explains what's missing.
bool findMods(fs::path &gd, fs::path &mods) {
    gd = findGeometryDash();
    if (gd.empty()) {
        std::fprintf(stderr,
                     "catberry cbmm: Geometry Dash was not found in your Steam libraries.\n"
                     "Install it with Steam, start it once, then try again.\n");
        return false;
    }
    std::error_code ec;
    if (!fs::exists(gd / "geode", ec)) {
        std::fprintf(stderr,
                     "catberry cbmm: Geode is not installed in %s.\n"
                     "Install Geode first: https://geode-sdk.org\n",
                     gd.string().c_str());
        return false;
    }
    mods = gd / "geode" / "mods";
    return true;
}

// The game and Geode versions, from the first line of Geode's newest log:
// "Running Geode v6.0.0 in Geometry Dash v2.2091 on Windows". Empty when
// Geode hasn't run yet.
void detectVersions(const fs::path &gd, std::string &game, std::string &geode) {
    game.clear();
    geode.clear();
    std::error_code ec;
    fs::path newest;
    fs::file_time_type newestTime{};
    for (auto const &entry : fs::directory_iterator(gd / "geode" / "logs", ec)) {
        if (entry.path().extension() != ".log") continue;
        auto t = entry.last_write_time(ec);
        if (newest.empty() || t > newestTime) {
            newest = entry.path();
            newestTime = t;
        }
    }
    if (newest.empty()) return;
    std::ifstream in(newest);
    std::string line;
    for (int i = 0; i < 40 && std::getline(in, line); i++) {
        auto at = line.find("Running Geode v");
        auto in_ = line.find(" in Geometry Dash v");
        if (at == std::string::npos || in_ == std::string::npos) continue;
        geode = line.substr(at + 15, in_ - (at + 15));
        auto rest = line.substr(in_ + 19);
        game = rest.substr(0, rest.find(' '));
        return;
    }
}

// The build for this game. Without a version (Geode never started) the
// newest one; nullptr (with a message) for a game version not built for.
const Build *pickBuild(const fs::path &gd, bool quiet = false) {
    std::string game, geode;
    detectVersions(gd, game, geode);
    if (game.empty()) {
        if (!quiet) std::printf("Couldn't tell the Geometry Dash version (start the game with Geode once); using the %s build.\n", kBuilds[0].gd);
        return &kBuilds[0];
    }
    for (auto const &b : kBuilds) {
        if (game != b.gd) continue;
        if (!quiet) std::printf("Geometry Dash %s with Geode v%s: using the build for GD %s.\n", game.c_str(), geode.c_str(), b.gd);
        if (!quiet && geode.rfind(b.geode, 0) != 0)
            std::printf("Note: this build is made for Geode %s.x; update Geode if the mod doesn't load.\n", b.geode);
        return &b;
    }
    std::fprintf(stderr, "catberry cbmm: Geometry Dash %s (Geode v%s) isn't supported yet: the mod is built for GD %s and %s.\n",
                 game.c_str(), geode.c_str(), kBuilds[0].gd, kBuilds[1].gd);
    return nullptr;
}

bool sameAs(const fs::path &file, const Build &b) {
    std::error_code ec;
    if (!fs::exists(file, ec) || fs::file_size(file, ec) != b.size()) return false;
    std::string text = readText(file);
    return text.size() == b.size() && std::memcmp(text.data(), b.start, b.size()) == 0;
}

int load() {
    fs::path gd, mods;
    if (!findMods(gd, mods)) return 1;
    const Build *build = pickBuild(gd);
    if (!build) return 1;
    std::error_code ec;
    fs::create_directories(mods, ec);
    fs::path target = mods / kModFile;
    fs::path partial = mods / (std::string(kModFile) + ".part");
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::fprintf(stderr, "catberry cbmm: cannot write to %s\n", mods.string().c_str());
            return 1;
        }
        out.write(reinterpret_cast<const char *>(build->start), static_cast<std::streamsize>(build->size()));
        if (!out) {
            std::fprintf(stderr, "catberry cbmm: writing %s failed\n", partial.string().c_str());
            fs::remove(partial, ec);
            return 1;
        }
    }
    fs::rename(partial, target, ec);
    if (ec) {
        std::fprintf(stderr, "catberry cbmm: could not replace %s: %s\n", target.string().c_str(), ec.message().c_str());
        fs::remove(partial, ec);
        return 1;
    }
    std::printf("%s %s (for GD %s) loaded into %s\n", kModName, CBMM_VERSION, build->gd, mods.string().c_str());
    std::printf("Restart Geometry Dash if it is open. Press Tab in game to open the menu.\n");
    return 0;
}

int unload() {
    fs::path gd, mods;
    if (!findMods(gd, mods)) return 1;
    std::error_code ec;
    fs::path target = mods / kModFile;
    if (!fs::exists(target, ec)) {
        std::printf("%s is not loaded in %s\n", kModName, mods.string().c_str());
        return 0;
    }
    if (!fs::remove(target, ec) || ec) {
        std::fprintf(stderr, "catberry cbmm: could not remove %s: %s\n", target.string().c_str(),
                     ec ? ec.message().c_str() : "unknown error");
        return 1;
    }
    std::printf("%s removed from %s\n", kModName, mods.string().c_str());
    std::printf("Restart Geometry Dash if it is open. Your settings and macros are kept.\n");
    return 0;
}

int status() {
    fs::path gd = findGeometryDash();
    std::printf("catberry cbmm %s\n", CBMM_VERSION);
    if (gd.empty()) {
        std::printf("Geometry Dash: not found in your Steam libraries\n");
        return 1;
    }
    std::printf("Geometry Dash: %s\n", gd.string().c_str());
    std::error_code ec;
    if (!fs::exists(gd / "geode", ec)) {
        std::printf("Geode: not installed\n");
        return 1;
    }
    std::string game, geode;
    detectVersions(gd, game, geode);
    if (!game.empty()) std::printf("Game: Geometry Dash %s, Geode v%s\n", game.c_str(), geode.c_str());
    fs::path target = gd / "geode" / "mods" / kModFile;
    const Build *build = pickBuild(gd, true);
    bool same = false;
    for (auto const &b : kBuilds) same = same || sameAs(target, b);
    if (!fs::exists(target, ec)) std::printf("%s: not loaded (run: catberry cbmm load)\n", kModName);
    else if (build && sameAs(target, *build)) std::printf("%s: loaded, version %s (for GD %s)\n", kModName, CBMM_VERSION, build->gd);
    else if (same) std::printf("%s: loaded, but the build for another GD version (run: catberry cbmm load)\n", kModName);
    else std::printf("%s: loaded, a different version (run: catberry cbmm load to use %s)\n", kModName, CBMM_VERSION);
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    std::string command = argc > 1 ? argv[1] : "";
    if (command == "-v" || command == "--version") {
        std::printf("catberry cbmm %s\n", CBMM_VERSION);
        return 0;
    }
    if (command == "load") return load();
    if (command == "unload") return unload();
    if (command == "status") return status();
    if (command.empty() || command == "-help" || command == "--help" || command == "help") {
        usage(stdout);
        return 0;
    }
    std::fprintf(stderr, "catberry cbmm: unknown command '%s'\n\n", command.c_str());
    usage(stderr);
    return 2;
}
