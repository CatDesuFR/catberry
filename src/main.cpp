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

// The mod file, embedded at build time.
#ifdef _WIN32
#define CBMM_SECTION ".rdata"
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
        ".previous\n");
extern "C" const unsigned char cbmm_geode_start[];
extern "C" const unsigned char cbmm_geode_end[];

namespace {

const char *const kModFile = "catdesu.cats-mod-menu.geode";
const char *const kModName = "Catberry's Mod Menu";

size_t modSize() { return static_cast<size_t>(cbmm_geode_end - cbmm_geode_start); }

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
                 "(https://geode-sdk.org). Restart the game after load or unload.\n",
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

bool sameAsEmbedded(const fs::path &file) {
    std::error_code ec;
    if (!fs::exists(file, ec) || fs::file_size(file, ec) != modSize()) return false;
    std::string text = readText(file);
    return text.size() == modSize() && std::memcmp(text.data(), cbmm_geode_start, modSize()) == 0;
}

int load() {
    fs::path gd, mods;
    if (!findMods(gd, mods)) return 1;
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
        out.write(reinterpret_cast<const char *>(cbmm_geode_start), static_cast<std::streamsize>(modSize()));
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
    std::printf("%s %s loaded into %s\n", kModName, CBMM_VERSION, mods.string().c_str());
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
    fs::path target = gd / "geode" / "mods" / kModFile;
    if (!fs::exists(target, ec)) std::printf("%s: not loaded (run: catberry cbmm load)\n", kModName);
    else if (sameAsEmbedded(target)) std::printf("%s: loaded, version %s\n", kModName, CBMM_VERSION);
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
