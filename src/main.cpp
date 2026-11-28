// catberry scan: a PC check for Roblox, Counter-Strike 2 and Easy Anti-Cheat
// games (Windows). Looks for known cheats and cheat tools (files, running
// programs, programs run before, drivers) and for traces of code injected into
// running programs, and writes a signed report, results_scan_<id>.txt, that
// `catberry scan verify` checks for changes.
//
// It never opens the game itself (the game must be closed): reading a
// protected game's memory looks like cheating to its anti-cheat.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>
#include <shlobj.h>
#include <softpub.h>
#include <tlhelp32.h>
#include <wintrust.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "signatures.h"

#ifndef SCAN_VERSION
#define SCAN_VERSION "dev"
#endif
// The signing key (SCAN_KEY_HEX) is given when the official release is built
// and is not in the source code; a build without one says so in its reports.
// It is kept scrambled in the exe, so it isn't sitting there as plain text
// (someone determined can still dig it out of the program).
#ifndef SCAN_KEY_HEX
#define SCAN_KEY_HEX ""
#endif

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

std::string utf8(const std::wstring &w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring wide(const std::string &s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string lower(std::string s) {
    for (char &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool endsWith(const std::string &s, const char *end) {
    size_t n = std::strlen(end);
    return s.size() >= n && s.compare(s.size() - n, n, end) == 0;
}

std::string hex(const unsigned char *data, size_t n) {
    static const char *digits = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; i++) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}

std::string env(const char *name) {
    wchar_t buf[32768];
    DWORD n = GetEnvironmentVariableW(wide(name).c_str(), buf, 32768);
    return n && n < 32768 ? utf8(std::wstring(buf, n)) : "";
}

std::string localTime(const FILETIME &ft) {
    FILETIME local;
    SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(&ft, &local) || !FileTimeToSystemTime(&local, &st)) return "?";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

// ---------------------------------------------------------------------------
// Hashing and signing (Windows' own crypto)
// ---------------------------------------------------------------------------

std::string digest(const std::string &data, const std::string &key) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char out[32] = {};
    bool hmac = !key.empty();
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, hmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0) != 0)
        return "";
    if (BCryptCreateHash(alg, &hash, nullptr, 0, hmac ? (PUCHAR)key.data() : nullptr, ULONG(key.size()), 0) == 0) {
        BCryptHashData(hash, (PUCHAR)data.data(), ULONG(data.size()), 0);
        BCryptFinishHash(hash, out, sizeof out, 0);
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return hex(out, sizeof out);
}

constexpr char kKeyHex[] = SCAN_KEY_HEX;
struct Hidden {
    unsigned char b[sizeof kKeyHex];
};
constexpr Hidden hide() {
    Hidden h{};
    for (size_t i = 0; i < sizeof kKeyHex; i++) h.b[i] = static_cast<unsigned char>(kKeyHex[i] ^ (0x5a + i * 7));
    return h;
}
constexpr Hidden kHidden = hide();

bool officialBuild() { return sizeof kKeyHex > 1; }

std::string signingKey() {
    if (!officialBuild()) return "catberry scan unofficial build";
    static const volatile unsigned char *hidden = kHidden.b;
    std::string key;
    for (size_t i = 0; i + 1 < sizeof kKeyHex; i++) key += char(hidden[i] ^ static_cast<unsigned char>(0x5a + i * 7));
    return key;
}

// Which key signed a report, without giving the key away.
std::string keyId() { return digest("catberry scan key id:" + signingKey(), "").substr(0, 12); }

std::string sign(const std::string &text) { return digest(text, signingKey()); }

// ---------------------------------------------------------------------------
// Findings
// ---------------------------------------------------------------------------

enum class Level { Cheat, Tool, Unidentified, Warning };

const char *levelName(Level l) {
    switch (l) {
    case Level::Cheat: return "CHEAT";
    case Level::Tool: return "CHEAT TOOL";
    case Level::Unidentified: return "UNIDENTIFIED";
    case Level::Warning: return "WARNING";
    }
    return "?";
}

struct Finding {
    Level level;
    std::string what;  // what it is
    std::string where; // where it was seen
};

std::vector<Finding> g_findings;
std::set<std::string> g_seen;
std::vector<std::string> g_checked; // what the scan looked at, for the report
std::vector<std::string> g_skipped; // checks that could not run

void found(Level level, const std::string &what, const std::string &where) {
    if (!g_seen.insert(std::to_string(int(level)) + "|" + what + "|" + lower(where)).second) return;
    g_findings.push_back({level, what, where});
}

unsigned g_game = 0;

// The signature a name (of a file, folder, program or DLL) matches, if any.
// isProgram: an .exe or .dll, or a running program.
const Signature *matchName(std::string name, bool isProgram) {
    name = lower(name);
    std::string stem = name;
    for (const char *ext : {".exe", ".dll", ".sys", ".pf", ".zip", ".rar", ".7z", ".lnk"}) {
        size_t n = std::strlen(ext);
        if (stem.size() > n && stem.compare(stem.size() - n, n, ext) == 0) {
            stem.resize(stem.size() - n);
            break;
        }
    }
    for (const Signature &s : kSignatures) {
        if (!(s.games & g_game)) continue;
        switch (s.match) {
        case Match::Name:
            if (stem == s.text) return &s;
            break;
        case Match::Contains:
            if (name.find(s.text) != std::string::npos) return &s;
            break;
        case Match::ExeOnly:
            if (isProgram && stem == s.text) return &s;
            break;
        }
    }
    return nullptr;
}

bool isProgramName(const std::string &name) {
    std::string l = lower(name);
    return l.size() > 4 && (endsWith(l, ".exe") || endsWith(l, ".dll"));
}

void checkName(const std::string &name, bool isProgram, const std::string &where) {
    if (const Signature *s = matchName(name, isProgram))
        found(s->kind == Kind::Cheat ? Level::Cheat : Level::Tool, s->label, where);
}

// ---------------------------------------------------------------------------
// System facts
// ---------------------------------------------------------------------------

bool isAdmin() {
    HANDLE token = nullptr;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    bool admin = false;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof elevation, &size)) admin = elevation.TokenIsElevated;
        CloseHandle(token);
    }
    return admin;
}

std::string windowsVersion() {
    HKEY key;
    std::string out = "Windows";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key) == 0) {
        wchar_t buf[256];
        DWORD size = sizeof buf, type;
        if (RegQueryValueExW(key, L"ProductName", nullptr, &type, (LPBYTE)buf, &size) == 0) out = utf8(buf);
        size = sizeof buf;
        if (RegQueryValueExW(key, L"CurrentBuildNumber", nullptr, &type, (LPBYTE)buf, &size) == 0) out += " build " + utf8(buf);
        RegCloseKey(key);
    }
    return out;
}

std::string knownFolder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    std::string out;
    if (SHGetKnownFolderPath(id, 0, nullptr, &path) == S_OK) out = utf8(path);
    CoTaskMemFree(path);
    return out;
}

std::string regString(HKEY root, const wchar_t *path, const wchar_t *value) {
    HKEY key;
    std::string out;
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &key) != 0) return out;
    wchar_t buf[4096];
    DWORD size = sizeof buf - sizeof(wchar_t), type;
    if (RegQueryValueExW(key, value, nullptr, &type, (LPBYTE)buf, &size) == 0 && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        buf[size / sizeof(wchar_t)] = 0;
        out = utf8(buf);
    }
    RegCloseKey(key);
    return out;
}

// True when the file carries a valid digital signature (a planted DLL
// usually has none; Microsoft's and the game's own do).
bool isSigned(const std::string &path) {
    std::wstring w = wide(path);
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof file;
    file.pcwszFilePath = w.c_str();
    WINTRUST_DATA data{};
    data.cbStruct = sizeof data;
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG result = WinVerifyTrust(nullptr, &action, &data);
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &data);
    return result == 0;
}

// ---------------------------------------------------------------------------
// Running programs
// ---------------------------------------------------------------------------

struct Process {
    DWORD pid;
    std::string name;
};

std::vector<Process> processes() {
    std::vector<Process> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof e;
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) out.push_back({e.th32ProcessID, utf8(e.szExeFile)});
    CloseHandle(snap);
    return out;
}

std::string processPath(HANDLE h) {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = MAX_PATH * 4;
    return QueryFullProcessImageNameW(h, 0, buf, &n) ? utf8(std::wstring(buf, n)) : "";
}

std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> out;
    std::stringstream in(s);
    std::string part;
    while (std::getline(in, part, sep))
        if (!part.empty()) out.push_back(part);
    return out;
}

// The game's own programs (and its anti-cheat): it must be closed for the scan.
std::vector<std::string> gameProcessNames() {
    std::vector<std::string> names;
    if (g_game & Roblox) names.insert(names.end(), {"robloxplayerbeta.exe", "robloxstudiobeta.exe", "windows10universal.exe"});
    if (g_game & CS2) names.insert(names.end(), {"cs2.exe"});
    for (const AntiCheat &ac : kAntiCheats)
        if (g_game & ac.game)
            for (const std::string &p : split(ac.processes, ',')) names.push_back(p);
    return names;
}

std::vector<std::string> runningGame() {
    std::vector<std::string> out;
    auto names = gameProcessNames();
    for (const Process &p : processes())
        if (std::find(names.begin(), names.end(), lower(p.name)) != names.end()) out.push_back(p.name);
    return out;
}

using NtQueryInformationThreadFn = LONG(NTAPI *)(HANDLE, ULONG, PVOID, ULONG, PULONG);

bool executable(DWORD protect) {
    if (protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    return protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
}

bool hasPeHeader(HANDLE h, LPCVOID base) {
    unsigned char head[0x400] = {};
    SIZE_T got = 0;
    if (!ReadProcessMemory(h, base, head, sizeof head, &got) || got < 0x40) return false;
    if (head[0] != 'M' || head[1] != 'Z') return false;
    DWORD peAt = *reinterpret_cast<DWORD *>(head + 0x3c);
    return peAt + 4 <= got && std::memcmp(head + peAt, "PE\0\0", 4) == 0;
}

std::string addr(ULONG_PTR a) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)a);
    return buf;
}

// Every running program (except the game, which is closed, and this scan):
//  - its name and its DLLs against the cheat names;
//  - executable memory that no file backs, holding a program (a DLL loaded
//    by hand, the way injectors hide one);
//  - a program image whose file is gone (a program swapped in memory);
//  - threads that start in executable memory no file backs (injected code).
void scanProcesses() {
    auto procs = processes();
    int opened = 0, closed = 0;
    auto ntQuery = reinterpret_cast<NtQueryInformationThreadFn>(
        reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread")));
    std::map<DWORD, std::vector<DWORD>> threads;
    if (HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 t{};
        t.dwSize = sizeof t;
        for (BOOL ok = Thread32First(snap, &t); ok; ok = Thread32Next(snap, &t)) threads[t.th32OwnerProcessID].push_back(t.th32ThreadID);
        CloseHandle(snap);
    }
    for (const Process &p : procs) {
        if (p.pid <= 4 || p.pid == GetCurrentProcessId()) continue;
        checkName(p.name, true, "running program " + p.name + " (pid " + std::to_string(p.pid) + ")");
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, p.pid);
        if (!h) {
            closed++;
            continue;
        }
        opened++;
        std::string path = processPath(h);
        std::string who = p.name + " (pid " + std::to_string(p.pid) + (path.empty() ? "" : ", " + path) + ")";

        HMODULE mods[2048];
        DWORD need = 0;
        if (EnumProcessModulesEx(h, mods, sizeof mods, &need, LIST_MODULES_ALL)) {
            for (DWORD i = 0; i < std::min<DWORD>(need / sizeof(HMODULE), 2048); i++) {
                wchar_t file[MAX_PATH * 2];
                if (!GetModuleFileNameExW(h, mods[i], file, MAX_PATH * 2)) continue;
                std::string f = utf8(file);
                checkName(fs::path(file).filename().u8string(), true, "DLL " + f + " loaded in " + who);
            }
        }

        MEMORY_BASIC_INFORMATION mbi;
        int traces = 0;
        for (ULONG_PTR a = 0; VirtualQueryEx(h, (LPCVOID)a, &mbi, sizeof mbi) == sizeof mbi && traces < 20;
             a = (ULONG_PTR)mbi.BaseAddress + mbi.RegionSize) {
            if (mbi.State != MEM_COMMIT || mbi.BaseAddress != mbi.AllocationBase) continue;
            if (mbi.Type == MEM_PRIVATE && executable(mbi.Protect) && hasPeHeader(h, mbi.BaseAddress)) {
                found(Level::Unidentified, "program loaded into memory by hand (not from a file), at " + addr((ULONG_PTR)mbi.BaseAddress),
                      "memory of " + who);
                traces++;
            } else if (mbi.Type == MEM_IMAGE) {
                wchar_t mapped[MAX_PATH * 2];
                if (!GetMappedFileNameW(h, mbi.BaseAddress, mapped, MAX_PATH * 2) && GetLastError() != ERROR_ACCESS_DENIED &&
                    hasPeHeader(h, mbi.BaseAddress)) {
                    found(Level::Unidentified, "program image in memory whose file is gone, at " + addr((ULONG_PTR)mbi.BaseAddress),
                          "memory of " + who);
                    traces++;
                }
            }
            if (a > (ULONG_PTR)0x7fffffffffff) break;
        }

        if (ntQuery) {
            int injected = 0;
            for (DWORD tid : threads[p.pid]) {
                HANDLE th = OpenThread(THREAD_QUERY_INFORMATION, FALSE, tid);
                if (!th) continue;
                PVOID start = nullptr;
                if (ntQuery(th, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof start, nullptr) == 0 && start &&
                    VirtualQueryEx(h, start, &mbi, sizeof mbi) == sizeof mbi && mbi.Type == MEM_PRIVATE && executable(mbi.Protect)) {
                    if (injected++ < 5)
                        found(Level::Unidentified,
                              "thread " + std::to_string(tid) + " started in memory no file backs (injected code), at " + addr((ULONG_PTR)start),
                              "threads of " + who);
                }
                CloseHandle(th);
            }
        }
        CloseHandle(h);
    }
    g_checked.push_back(std::to_string(procs.size()) + " running programs: their names, DLLs, memory and threads (" + std::to_string(opened) +
                        " opened" + (closed ? ", " + std::to_string(closed) + " could not be opened" : "") + ")");
}

// ---------------------------------------------------------------------------
// Drivers and Windows settings
// ---------------------------------------------------------------------------

void scanDrivers() {
    LPVOID bases[2048];
    DWORD need = 0;
    if (!EnumDeviceDrivers(bases, sizeof bases, &need)) {
        g_skipped.push_back("loaded drivers (Windows did not list them)");
        return;
    }
    DWORD n = std::min<DWORD>(need / sizeof(LPVOID), 2048);
    std::string userRoot = lower(env("SystemDrive") + "\\users\\");
    for (DWORD i = 0; i < n; i++) {
        wchar_t name[MAX_PATH], file[MAX_PATH * 2];
        if (!GetDeviceDriverBaseNameW(bases[i], name, MAX_PATH)) continue;
        std::string base = lower(utf8(name));
        for (const DriverSignature &d : kDrivers)
            if (base == d.file) found(Level::Tool, d.label, "loaded driver " + utf8(name));
        if (GetDeviceDriverFileNameW(bases[i], file, MAX_PATH * 2)) {
            std::string f = lower(utf8(file));
            if (f.find("\\users\\") != std::string::npos || f.find("\\temp\\") != std::string::npos ||
                f.find("\\downloads\\") != std::string::npos)
                found(Level::Unidentified, "driver loaded from a user or temporary folder", "loaded driver " + utf8(file));
        }
    }
    g_checked.push_back(std::to_string(n) + " loaded drivers");
}

void scanBootSettings() {
    std::string opts = lower(regString(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", L"SystemStartOptions"));
    if (opts.find("testsigning") != std::string::npos)
        found(Level::Warning, "Windows test mode is on: drivers without a valid signature can load (cheat drivers use this)",
              "boot options");
    if (opts.find("disable_integrity_checks") != std::string::npos)
        found(Level::Warning, "driver signature checks are turned off", "boot options");
    g_checked.push_back("Windows boot options (test mode, driver signature checks)");
}

// ---------------------------------------------------------------------------
// Files on disk
// ---------------------------------------------------------------------------

size_t g_files = 0;
const size_t kMaxFiles = 400000;

// Folders that hold Roblox executor scripts: an executor's own folder has
// both, whatever it is called.
bool looksLikeExecutorFolder(const fs::path &dir) {
    std::error_code ec;
    return fs::is_directory(dir / "autoexec", ec) && fs::is_directory(dir / "workspace", ec);
}

// Function names only a Roblox executor exposes (the exploit API). A renamed
// executor still has them inside it as plain text.
//
// They are kept XOR-scrambled so they are NOT sitting in our own program as
// plain text: otherwise catberry-scan.exe (which is in a scanned folder) would
// match itself. Decoded once at run time into the needles we actually search.
struct ApiEnc {
    unsigned char b[24];
    int n;
};
constexpr ApiEnc apiEnc(const char *s) {
    ApiEnc e{};
    int i = 0;
    for (; s[i] && i < 24; i++) e.b[i] = static_cast<unsigned char>(s[i]) ^ 0x5a;
    e.n = i;
    return e;
}
constexpr ApiEnc kApiEnc[] = {
    apiEnc("getgenv"),          apiEnc("hookfunction"),    apiEnc("hookmetamethod"),    apiEnc("getrawmetatable"),
    apiEnc("setreadonly"),      apiEnc("newcclosure"),     apiEnc("getnamecallmethod"), apiEnc("identifyexecutor"),
    apiEnc("getscriptbytecode"), apiEnc("firesignal"),     apiEnc("getconnections"),    apiEnc("islclosure"),
    apiEnc("getsenv"),          apiEnc("replaceclosure"),  apiEnc("getloadedmodules"),  apiEnc("getrunningscripts"),
    apiEnc("setfpscap"),        apiEnc("make_writable"),   apiEnc("getrenv"),           apiEnc("getcallingscript"),
};

const std::vector<std::string> &exploitNeedles() {
    static const std::vector<std::string> needles = [] {
        std::vector<std::string> v;
        for (const ApiEnc &e : kApiEnc) {
            std::string s;
            for (int i = 0; i < e.n; i++) s += char(e.b[i] ^ 0x5a);
            v.push_back(s);
        }
        return v;
    }();
    return needles;
}
size_t g_contentScanned = 0;

// An executable that holds several of those names is almost certainly a Roblox
// executor, whatever it is named. Reads the file (big ones are read in part).
bool contentLooksLikeExecutor(const fs::path &p, std::string &which) {
    std::error_code ec;
    auto size = fs::file_size(p, ec);
    if (ec || size < 4096 || size > 300ull * 1024 * 1024) return false;
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::string buf(static_cast<size_t>(std::min<uintmax_t>(size, 160ull * 1024 * 1024)), '\0');
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    buf.resize(static_cast<size_t>(in.gcount()));
    g_contentScanned++;
    int hits = 0, shown = 0;
    which.clear();
    for (const std::string &api : exploitNeedles())
        if (buf.find(api) != std::string::npos) {
            hits++;
            if (shown < 6) {
                if (shown) which += ", ";
                which += api;
                shown++;
            }
        }
    if (hits > shown) which += " +" + std::to_string(hits - shown) + " more";
    return hits >= 4;
}

// contentScan: read .exe files here for the exploit API (off in large,
// trusted trees like Program Files, to stay fast).
void walk(const fs::path &dir, int depth, bool contentScan) {
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::directory_iterator() && g_files < kMaxFiles; it.increment(ec)) {
        const fs::directory_entry &e = *it;
        g_files++;
        std::string name = e.path().filename().u8string();
        bool isDir = e.is_directory(ec) && !e.is_symlink(ec);
        checkName(name, !isDir && isProgramName(name), (isDir ? "folder " : "file ") + e.path().u8string());
        if (!isDir) {
            if (contentScan && (g_game & Roblox) && endsWith(lower(name), ".exe")) {
                std::string which;
                if (contentLooksLikeExecutor(e.path(), which))
                    found(Level::Cheat,
                          "program holds Roblox exploit functions (" + which + "): a Roblox executor, even renamed",
                          "file " + e.path().u8string());
            }
            continue;
        }
        std::string l = lower(name);
        // Skip only the real system folder, not every folder named "Windows"
        // (AppData\Local\Microsoft\Windows\INetCache is a real hiding spot).
        static const std::string sysroot = lower(env("SystemRoot"));
        if (!sysroot.empty() && lower(e.path().u8string()) == sysroot) continue;
        if (l == "node_modules" || l == ".git" || l == "winsxs" || l == "$recycle.bin" || l == "packages") continue;
        if ((g_game & Roblox) && looksLikeExecutorFolder(e.path()))
            found(Level::Cheat, "Roblox executor folder (has autoexec and workspace script folders)", "folder " + e.path().u8string());
        DWORD attrs = GetFileAttributesW(e.path().c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        if (depth > 0) walk(e.path(), depth - 1, contentScan);
    }
}

void scanFiles() {
    std::string home = env("USERPROFILE"), local = env("LOCALAPPDATA"), roaming = env("APPDATA");
    // {folder, how deep, read .exe contents here}. Executors hide deep in
    // AppData, so those go deep; Program Files is large and trusted, so it is
    // shallower and its .exe files are not read.
    struct Root {
        std::string path;
        int depth;
        bool content;
    };
    std::vector<Root> roots = {
        {knownFolder(FOLDERID_Desktop), 4, true}, {knownFolder(FOLDERID_Downloads), 5, true},
        {knownFolder(FOLDERID_Documents), 4, true}, {local, 7, true},
        {roaming, 7, true},                       {env("TEMP"), 5, true},
        {env("ProgramFiles"), 3, false},          {env("ProgramFiles(x86)"), 3, false},
        {env("ProgramData"), 4, true},            {home, 2, true},
    };
    // Every other drive's top folders too (cheats often sit on D:\).
    DWORD drives = GetLogicalDrives();
    std::string system = lower(env("SystemDrive"));
    for (int i = 0; i < 26; i++) {
        if (!(drives & (1u << i))) continue;
        std::string root = std::string(1, char('A' + i)) + ":\\";
        if (GetDriveTypeA(root.c_str()) != DRIVE_FIXED) continue;
        if (lower(root.substr(0, 2)) == system) continue;
        roots.push_back({root, 3, true});
    }
    std::set<std::string> done;
    int n = 0;
    for (const Root &r : roots) {
        if (r.path.empty() || !done.insert(lower(r.path)).second) continue;
        walk(fs::u8path(r.path), r.depth, r.content);
        n++;
    }
    g_checked.push_back(std::to_string(g_files) + " files and folders in " + std::to_string(n) +
                        " places (desktop, downloads, documents, app data, temp, program files, other drives)" +
                        (g_files >= kMaxFiles ? " (stopped at the limit)" : "") +
                        ((g_game & Roblox) ? ", " + std::to_string(g_contentScanned) + " programs read for Roblox exploit functions"
                                           : ""));
}

// ---------------------------------------------------------------------------
// Programs run before
// ---------------------------------------------------------------------------

std::string rot13(std::string s) {
    for (char &c : s) {
        if (c >= 'a' && c <= 'z') c = char('a' + (c - 'a' + 13) % 26);
        else if (c >= 'A' && c <= 'Z') c = char('A' + (c - 'A' + 13) % 26);
    }
    return s;
}

void checkRunPath(const std::string &path, const std::string &source) {
    std::string name = path.substr(path.find_last_of("\\/") + 1);
    checkName(name, isProgramName(name), source + ": " + path);
    if (const Signature *s = matchName(path, false); s && s->match == Match::Contains)
        found(s->kind == Kind::Cheat ? Level::Cheat : Level::Tool, s->label, source + ": " + path);
}

// Windows' prefetch: the programs run lately (administrator only).
void scanPrefetch() {
    std::string dir = env("SystemRoot") + "\\Prefetch";
    std::error_code ec;
    int n = 0;
    for (fs::directory_iterator it(fs::u8path(dir), ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::string name = it->path().filename().u8string();
        if (!endsWith(lower(name), ".pf")) continue;
        n++;
        // NAME.EXE-1A2B3C4D.pf
        std::string exe = name.substr(0, name.rfind('-'));
        WIN32_FILE_ATTRIBUTE_DATA a;
        std::string when = GetFileAttributesExW(it->path().c_str(), GetFileExInfoStandard, &a) ? localTime(a.ftLastWriteTime) : "?";
        checkName(exe, true, "program run (prefetch, last " + when + "): " + exe);
    }
    if (n == 0) g_skipped.push_back("prefetch (programs run lately): needs administrator, or prefetch is off");
    else g_checked.push_back(std::to_string(n) + " prefetch entries (programs run lately)");
}

// Background Activity Moderator: every program run, with when (administrator only).
void scanBam() {
    const wchar_t *roots[] = {L"SYSTEM\\CurrentControlSet\\Services\\bam\\State\\UserSettings",
                              L"SYSTEM\\CurrentControlSet\\Services\\bam\\UserSettings"};
    int n = 0;
    for (const wchar_t *root : roots) {
        HKEY key;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, root, 0, KEY_READ, &key) != 0) continue;
        wchar_t sid[256];
        for (DWORD i = 0;; i++) {
            DWORD len = 256;
            if (RegEnumKeyExW(key, i, sid, &len, nullptr, nullptr, nullptr, nullptr) != 0) break;
            HKEY user;
            if (RegOpenKeyExW(key, sid, 0, KEY_READ, &user) != 0) continue;
            wchar_t value[2048];
            BYTE data[64];
            for (DWORD j = 0;; j++) {
                DWORD vlen = 2048, dlen = sizeof data, type;
                if (RegEnumValueW(user, j, value, &vlen, nullptr, &type, data, &dlen) != 0) break;
                if (type != REG_BINARY || dlen < 8) continue;
                n++;
                FILETIME ft;
                std::memcpy(&ft, data, sizeof ft);
                checkRunPath(utf8(value), "program run (BAM, last " + localTime(ft) + ")");
            }
            RegCloseKey(user);
        }
        RegCloseKey(key);
    }
    if (n == 0) g_skipped.push_back("BAM (every program run, with when): needs administrator");
    else g_checked.push_back(std::to_string(n) + " BAM entries (programs run, with when)");
}

// This user's MuiCache and UserAssist: programs opened from Explorer.
void scanUserHistory() {
    int n = 0;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache", 0,
                      KEY_READ, &key) == 0) {
        wchar_t value[2048];
        for (DWORD i = 0;; i++) {
            DWORD len = 2048;
            if (RegEnumValueW(key, i, value, &len, nullptr, nullptr, nullptr, nullptr) != 0) break;
            std::string v = utf8(value);
            size_t dot = lower(v).rfind(".exe.");
            if (dot != std::string::npos) v = v.substr(0, dot + 4);
            n++;
            checkRunPath(v, "program opened (MuiCache)");
        }
        RegCloseKey(key);
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\UserAssist", 0, KEY_READ, &key) == 0) {
        wchar_t guid[256];
        for (DWORD i = 0;; i++) {
            DWORD len = 256;
            if (RegEnumKeyExW(key, i, guid, &len, nullptr, nullptr, nullptr, nullptr) != 0) break;
            HKEY count;
            if (RegOpenKeyExW(key, (std::wstring(guid) + L"\\Count").c_str(), 0, KEY_READ, &count) != 0) continue;
            wchar_t value[2048];
            BYTE data[128];
            for (DWORD j = 0;; j++) {
                DWORD vlen = 2048, dlen = sizeof data, type;
                if (RegEnumValueW(count, j, value, &vlen, nullptr, &type, data, &dlen) != 0) break;
                n++;
                std::string when;
                if (dlen >= 68) {
                    FILETIME ft;
                    std::memcpy(&ft, data + 60, sizeof ft);
                    if (ft.dwHighDateTime) when = ", last " + localTime(ft);
                }
                checkRunPath(rot13(utf8(value)), "program opened (UserAssist" + when + ")");
            }
            RegCloseKey(count);
        }
        RegCloseKey(key);
    }
    g_checked.push_back(std::to_string(n) + " entries of programs opened from Explorer (MuiCache, UserAssist)");
}

// ---------------------------------------------------------------------------
// The games' own folders
// ---------------------------------------------------------------------------

// DLLs in a game folder that Windows would load into the game instead of its
// own copy, without a valid signature.
void checkProxyDlls(const fs::path &dir, const std::string &game) {
    for (const char *dll : kProxyDlls) {
        fs::path f = dir / dll;
        std::error_code ec;
        if (!fs::is_regular_file(f, ec) || isSigned(f.u8string())) continue;
        found(Level::Warning,
              std::string("unsigned ") + dll + " next to the " + game +
                  " program: it runs inside the game (cheats get in this way; ReShade and some mods do too)",
              "file " + f.u8string());
    }
}

std::vector<fs::path> steamLibraries() {
    std::vector<fs::path> out;
    std::string steam = regString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (steam.empty()) steam = regString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    if (steam.empty()) return out;
    out.push_back(fs::u8path(steam));
    std::ifstream in(fs::u8path(steam) / "steamapps" / "libraryfolders.vdf");
    std::string line;
    while (std::getline(in, line)) {
        size_t k = line.find("\"path\"");
        if (k == std::string::npos) continue;
        size_t a = line.find('"', k + 6), b = a == std::string::npos ? a : line.find('"', a + 1);
        if (b == std::string::npos) continue;
        std::string p = line.substr(a + 1, b - a - 1), clean;
        for (size_t i = 0; i < p.size(); i++) {
            if (p[i] == '\\' && i + 1 < p.size() && p[i + 1] == '\\') i++;
            clean += p[i];
        }
        fs::path lib = fs::u8path(clean);
        std::error_code ec;
        bool dup = false;
        for (auto &o : out) dup |= fs::equivalent(o, lib, ec);
        if (!dup) out.push_back(lib);
    }
    return out;
}

void scanRobloxFolders() {
    int versions = 0;
    for (const char *base : {"Roblox", "Bloxstrap", "Fishstrap", "Voidstrap"}) {
        fs::path dir = fs::u8path(env("LOCALAPPDATA")) / base / "Versions";
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (!it->is_directory(ec)) continue;
            versions++;
            checkProxyDlls(it->path(), "Roblox");
            fs::path settings = it->path() / "ClientSettings" / "ClientAppSettings.json";
            if (fs::exists(settings, ec))
                found(Level::Warning, "Roblox's hidden settings (FFlags) are changed (Bloxstrap and similar launchers do this)",
                      "file " + settings.u8string());
        }
    }
    g_checked.push_back(std::to_string(versions) + " Roblox install folders (Roblox, Bloxstrap, Fishstrap, Voidstrap)");
}

void scanCs2Folders() {
    int games = 0;
    for (const fs::path &lib : steamLibraries()) {
        fs::path game = lib / "steamapps" / "common" / "Counter-Strike Global Offensive";
        std::error_code ec;
        if (!fs::is_directory(game, ec)) continue;
        games++;
        checkProxyDlls(game / "game" / "bin" / "win64", "CS2");
        checkProxyDlls(game / "game" / "csgo" / "bin" / "win64", "CS2");
    }
    // Launch options: -insecure turns VAC off (needed by some cheats).
    std::string steam = regString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(steam) / "userdata", ec); !steam.empty() && !ec && it != fs::directory_iterator();
         it.increment(ec)) {
        std::ifstream in(it->path() / "config" / "localconfig.vdf", std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        std::string text = ss.str();
        for (size_t at = text.find("\"730\""); at != std::string::npos; at = text.find("\"730\"", at + 5)) {
            size_t opt = text.find("\"LaunchOptions\"", at);
            if (opt == std::string::npos || opt - at > 3000) continue;
            size_t end = text.find('\n', opt);
            std::string line = lower(text.substr(opt, end - opt));
            if (line.find("-insecure") != std::string::npos)
                found(Level::Warning, "CS2 launch options have -insecure (turns VAC off; some cheats need it)",
                      "Steam settings " + (it->path() / "config" / "localconfig.vdf").u8string());
        }
    }
    g_checked.push_back(games ? "the CS2 game folder and its Steam launch options" : "CS2 game folder: not found in the Steam libraries");
}

// All game install folders from Steam and the Epic Games launcher.
std::vector<fs::path> installedGameFolders() {
    std::vector<fs::path> games;
    std::error_code ec;
    for (const fs::path &lib : steamLibraries())
        for (fs::directory_iterator it(lib / "steamapps" / "common", ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            if (it->is_directory(ec)) games.push_back(it->path());
    fs::path manifests = fs::u8path(env("ProgramData")) / "Epic" / "EpicGamesLauncher" / "Data" / "Manifests";
    for (fs::directory_iterator it(manifests, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::ifstream in(it->path());
        std::string line;
        while (std::getline(in, line)) {
            size_t k = line.find("\"InstallLocation\"");
            if (k == std::string::npos) continue;
            size_t a = line.find('"', line.find(':', k)), b = line.rfind('"');
            if (a == std::string::npos || b <= a) continue;
            std::string p = line.substr(a + 1, b - a - 1), clean;
            for (size_t i = 0; i < p.size(); i++) {
                if (p[i] == '\\' && i + 1 < p.size() && p[i + 1] == '\\') i++;
                clean += p[i];
            }
            if (!clean.empty()) games.push_back(fs::u8path(clean));
        }
    }
    return games;
}

// Games that keep a given anti-cheat's folder: check each for injected DLLs.
void scanAntiCheatGames(const AntiCheat &ac) {
    if (!*ac.gameSubdir) return;
    std::error_code ec;
    std::string names;
    int n = 0;
    for (const fs::path &g : installedGameFolders()) {
        if (!fs::is_directory(g / ac.gameSubdir, ec)) continue;
        n++;
        names += (names.empty() ? "" : ", ") + g.filename().u8string();
        fs::recursive_directory_iterator it(g, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (it.depth() > 4) {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file(ec) && lower(it->path().extension().u8string()) == ".exe")
                checkProxyDlls(it->path().parent_path(), g.filename().u8string());
        }
    }
    g_checked.push_back(n == 0 ? std::string(ac.name) + " games: none found in Steam or Epic Games"
                               : std::to_string(n) + " " + ac.name + " game folders: " + names);
}

// Note which anti-cheat is present on this PC (its driver loaded or its folder
// there). This is not a finding - it just says what the scan is looking at.
void scanAntiCheatPresence() {
    std::set<std::string> drivers;
    LPVOID bases[2048];
    DWORD need = 0;
    if (EnumDeviceDrivers(bases, sizeof bases, &need))
        for (DWORD i = 0, m = std::min<DWORD>(need / sizeof(LPVOID), 2048); i < m; i++) {
            wchar_t nm[MAX_PATH];
            if (GetDeviceDriverBaseNameW(bases[i], nm, MAX_PATH)) drivers.insert(lower(utf8(nm)));
        }
    for (const AntiCheat &ac : kAntiCheats) {
        if (!(g_game & ac.game)) continue;
        bool driverLoaded = *ac.driver && drivers.count(ac.driver);
        g_checked.push_back(std::string(ac.name) + (driverLoaded ? ": present (its driver is loaded)" : ": its driver is not loaded now"));
    }
}

// ---------------------------------------------------------------------------
// The report
// ---------------------------------------------------------------------------

std::string newScanId() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    unsigned char rnd[3] = {};
    BCryptGenRandom(nullptr, rnd, sizeof rnd, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d%02d%02d-%02d%02d%02d-%s", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                  hex(rnd, 3).c_str());
    return buf;
}

std::string nowText() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    TIME_ZONE_INFORMATION tz;
    DWORD z = GetTimeZoneInformation(&tz);
    long bias = -(tz.Bias + (z == TIME_ZONE_ID_DAYLIGHT ? tz.DaylightBias : 0));
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d (UTC%+ld:%02ld)", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                  st.wSecond, bias / 60, std::labs(bias % 60));
    return buf;
}

const char *gameName(unsigned g) {
    switch (g) {
    case Roblox: return "Roblox";
    case CS2: return "Counter-Strike 2";
    case Ezac: return "Easy Anti-Cheat games";
    case EchoAC: return "Echo AC (echo.ac) games";
    case BattlEye: return "BattlEye games";
    case Vanguard: return "Riot Vanguard games (Valorant, League)";
    case ACE: return "Anti-Cheat Expert games";
    }
    return "games";
}

const char *kSignatureLine = "signature: ";

std::string report(const std::string &id, const std::string &started, double seconds, bool admin) {
    std::ostringstream r;
    int counts[4] = {};
    for (auto &f : g_findings) counts[int(f.level)]++;
    r << "catberry scan " << SCAN_VERSION << " - PC check results\n";
    r << "Scan ID:  " << id << "\n";
    r << "Game:     " << gameName(g_game) << "\n";
    r << "Started:  " << started << ", took " << int(seconds) << " s\n";
    r << "System:   " << windowsVersion() << "\n";
    r << "Run as administrator: " << (admin ? "yes" : "NO (some checks could not run)") << "\n";
    r << "Build:    " << (officialBuild() ? "official" : "UNOFFICIAL (self-built: its signature proves nothing)") << ", key "
      << keyId() << "\n\n";
    if (g_findings.empty()) r << "RESULT: nothing found\n";
    else
        r << "RESULT: " << counts[0] << " cheat, " << counts[1] << " cheat tool, " << counts[2] << " unidentified (memory injection traces), "
          << counts[3] << " warning\n";
    r << "\n== Found ==\n";
    if (g_findings.empty()) r << "(nothing)\n";
    for (Level l : {Level::Cheat, Level::Tool, Level::Unidentified, Level::Warning})
        for (auto &f : g_findings)
            if (f.level == l) r << "[" << levelName(l) << "] " << f.what << "\n    seen: " << f.where << "\n";
    r << "\n== Checked ==\n";
    for (auto &c : g_checked) r << "- " << c << "\n";
    if (!g_skipped.empty()) {
        r << "\n== Not checked ==\n";
        for (auto &c : g_skipped) r << "- " << c << "\n";
    }
    r << "\nUNIDENTIFIED means code was found running from memory instead of a file, the way injected cheats run;\n"
         "it is not tied to a known cheat, and some programs with their own protection (or a JIT) can do it too.\n"
         "Check this report is unchanged with: catberry scan verify results_scan_"
      << id << ".txt\n";
    std::string body = r.str();
    // Windows line endings, so Notepad shows it right; the signature covers
    // the exact bytes.
    std::string out;
    for (char c : body) {
        if (c == '\n') out += '\r';
        out += c;
    }
    return out + kSignatureLine + sign(out) + "\r\n";
}

// Where the report goes: the current folder if it can be written to,
// otherwise Documents.
fs::path reportPath(const std::string &id) {
    std::string name = "results_scan_" + id + ".txt";
    std::error_code ec;
    fs::path here = fs::current_path(ec) / name;
    std::string sysroot = lower(env("SystemRoot"));
    if (!ec && lower(here.u8string()).rfind(sysroot, 0) != 0) {
        HANDLE h = CreateFileW(here.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            DeleteFileW(here.c_str());
            return here;
        }
    }
    return fs::u8path(knownFolder(FOLDERID_Documents)) / name;
}

int runScan(unsigned game) {
    g_game = game;
    auto running = runningGame();
    if (!running.empty()) {
        std::string list;
        for (auto &n : running) list += (list.empty() ? "" : ", ") + n;
        std::fprintf(stderr,
                     "Close %s first (running: %s).\n"
                     "The scan never looks inside the game: reading a protected game's memory looks like cheating to its anti-cheat.\n",
                     gameName(game), list.c_str());
        return 1;
    }
    bool admin = isAdmin();
    if (!admin)
        std::printf("Not running as administrator: some checks (programs run before, other users' programs) will be skipped.\n"
                    "For a full scan, open the terminal with \"Run as administrator\" and run it again.\n\n");
    std::string id = newScanId(), started = nowText();
    fs::path out = reportPath(id);
    // The report file is held open, with nobody else allowed to write it,
    // while the scan runs.
    HANDLE file = CreateFileW(out.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "catberry scan: cannot write %s\n", out.u8string().c_str());
        return 1;
    }
    auto t0 = std::chrono::steady_clock::now();
    std::printf("Scan %s for %s\n", id.c_str(), gameName(game));
    std::printf("  running programs, their memory and threads...\n");
    scanProcesses();
    std::printf("  drivers and boot options...\n");
    scanDrivers();
    scanBootSettings();
    std::printf("  programs run before...\n");
    scanPrefetch();
    scanBam();
    scanUserHistory();
    std::printf("  game folders...\n");
    if (game & Roblox) scanRobloxFolders();
    if (game & CS2) scanCs2Folders();
    scanAntiCheatPresence();
    for (const AntiCheat &ac : kAntiCheats)
        if (game & ac.game) scanAntiCheatGames(ac);
    std::printf("  files on disk...\n");
    scanFiles();
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::string text = report(id, started, seconds, admin);
    DWORD wrote = 0;
    bool ok = WriteFile(file, text.data(), DWORD(text.size()), &wrote, nullptr) && wrote == text.size();
    CloseHandle(file);
    if (!ok) {
        std::fprintf(stderr, "catberry scan: could not write %s\n", out.u8string().c_str());
        return 1;
    }
    int counts[4] = {};
    for (auto &f : g_findings) counts[int(f.level)]++;
    std::printf("\n");
    if (g_findings.empty()) std::printf("Nothing found.\n");
    else
        std::printf("Found: %d cheat, %d cheat tool, %d unidentified, %d warning.\n", counts[0], counts[1], counts[2], counts[3]);
    std::printf("Results: %s\n", out.u8string().c_str());
    return 0;
}

int verify(const std::string &path) {
    std::ifstream in(fs::u8path(path), std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "catberry scan: cannot open %s\n", path.c_str());
        return 2;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    size_t at = text.rfind(kSignatureLine);
    if (at == std::string::npos || (at > 0 && text[at - 1] != '\n')) {
        std::printf("NOT VALID: %s has no catberry scan signature.\n", path.c_str());
        return 1;
    }
    std::string body = text.substr(0, at), sig = text.substr(at + std::strlen(kSignatureLine));
    while (!sig.empty() && (sig.back() == '\n' || sig.back() == '\r' || sig.back() == ' ')) sig.pop_back();
    std::string id;
    if (size_t k = body.find("Scan ID:  "); k != std::string::npos) id = body.substr(k + 10, body.find('\r', k) - k - 10);
    std::string key;
    if (size_t k = body.find(", key "); k != std::string::npos) key = body.substr(k + 6, 12);
    if (sign(body) == sig) {
        std::printf("UNCHANGED: %s is the report catberry scan wrote (scan %s), not edited since.\n", path.c_str(), id.c_str());
        if (!officialBuild()) std::printf("(This catberry scan is a self-built copy: use the official one to check reports.)\n");
        return 0;
    }
    if (!key.empty() && key != keyId())
        std::printf("NOT VALID: %s was signed by a different catberry scan build (key %s, this one is %s).\n", path.c_str(), key.c_str(),
                    keyId().c_str());
    else std::printf("CHANGED: %s was edited after the scan wrote it. Do not trust it.\n", path.c_str());
    return 1;
}

void usage(FILE *to) {
    std::fprintf(to, "catberry scan %s - PC check for cheats (Windows)\n\n"
                     "  catberry scan roblox          scan for Roblox cheats\n"
                     "  catberry scan cs2             scan for Counter-Strike 2 cheats\n"
                     "  catberry scan ezac            Easy Anti-Cheat games\n"
                     "  catberry scan echoac          Echo AC (echo.ac) games: Rust, FiveM, DayZ...\n"
                     "  catberry scan battleye        BattlEye games: Rust, R6 Siege, PUBG...\n"
                     "  catberry scan vanguard        Riot Vanguard games: Valorant, League\n"
                     "  catberry scan ace             Anti-Cheat Expert games: Delta Force...\n"
                     "  catberry scan verify FILE     check a results_scan_<id>.txt was not edited\n"
                     "  catberry scan -v              print the version\n\n"
                     "Close the game first. Run the terminal as administrator for a full scan.\n"
                     "Each scan also looks for code injected into running programs (listed as unidentified).\n",
                 SCAN_VERSION);
}

} // namespace

int wmain(int argc, wchar_t **wargv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) args.push_back(utf8(wargv[i]));
    if (args.empty() || args[0] == "-h" || args[0] == "--help" || args[0] == "help") {
        usage(args.empty() ? stderr : stdout);
        return args.empty() ? 2 : 0;
    }
    std::string cmd = lower(args[0]);
    if (cmd == "-v" || cmd == "--version") {
        std::printf("catberry scan %s\n", SCAN_VERSION);
        return 0;
    }
    if (cmd == "verify") {
        if (args.size() != 2) {
            std::fprintf(stderr, "usage: catberry scan verify results_scan_<id>.txt\n");
            return 2;
        }
        return verify(args[1]);
    }
    static const std::pair<const char *, unsigned> kTargets[] = {
        {"roblox", Roblox}, {"cs2", CS2},        {"ezac", Ezac},        {"echoac", EchoAC},
        {"battleye", BattlEye}, {"vanguard", Vanguard}, {"ace", ACE},
    };
    if (args.size() == 1)
        for (auto &[word, game] : kTargets)
            if (cmd == word) {
                CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                return runScan(game);
            }
    std::fprintf(stderr, "catberry scan: unknown command '%s'\n\n", args[0].c_str());
    usage(stderr);
    return 2;
}
