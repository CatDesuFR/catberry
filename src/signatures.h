// What catberry scan looks for: names of known cheats and cheat tools, the
// drivers they load, and DLL names games load from their own folder (a
// planted copy runs inside the game).
#pragma once

enum Game : unsigned { Roblox = 1, CS2 = 2, Ezac = 4, AnyGame = 7 };

enum class Kind {
    Cheat,   // a known cheat for the game
    Tool,    // a tool used to cheat in any game (memory editors, injectors)
};

enum class Match {
    Name,     // the file, folder or process is called exactly this (without .exe/.dll)
    Contains, // the name contains this
    ExeOnly,  // like Name, but only programs and DLLs count (the name is a common word)
};

struct Signature {
    const char *text; // lower case
    const char *label;
    Kind kind;
    Match match;
    unsigned games;
};

inline const Signature kSignatures[] = {
    // Roblox executors.
    {"solara", "Solara (Roblox executor)", Kind::Cheat, Match::Name, Roblox},
    {"krnl", "KRNL (Roblox executor)", Kind::Cheat, Match::Name, Roblox},
    {"fluxus", "Fluxus (Roblox executor)", Kind::Cheat, Match::Name, Roblox},
    {"jjsploit", "JJSploit (Roblox executor)", Kind::Cheat, Match::Contains, Roblox},
    {"synapse x", "Synapse X (Roblox executor)", Kind::Cheat, Match::Contains, Roblox},
    {"synapsex", "Synapse X (Roblox executor)", Kind::Cheat, Match::Contains, Roblox},
    {"script-ware", "Script-Ware (Roblox executor)", Kind::Cheat, Match::Contains, Roblox},
    {"scriptware", "Script-Ware (Roblox executor)", Kind::Cheat, Match::Contains, Roblox},
    {"oxygen u", "Oxygen U (Roblox executor)", Kind::Cheat, Match::Contains, Roblox},
    {"sirhurt", "SirHurt (Roblox executor)", Kind::Cheat, Match::Contains, Roblox},
    {"kiwi x", "Kiwi X (Roblox executor)", Kind::Cheat, Match::Contains, Roblox},
    {"coco z", "Coco Z (Roblox executor)", Kind::Cheat, Match::Contains, Roblox},
    {"valyse", "Valyse (Roblox executor)", Kind::Cheat, Match::Contains, Roblox},
    {"seliware", "Seliware (Roblox executor)", Kind::Cheat, Match::Contains, Roblox},
    {"zorara", "Zorara (Roblox executor)", Kind::Cheat, Match::Contains, Roblox},
    {"nezur", "Nezur (Roblox executor)", Kind::Cheat, Match::Contains, Roblox},
    {"bunni.lol", "Bunni (Roblox executor)", Kind::Cheat, Match::Contains, Roblox},
    {"arceus x", "Arceus X (Roblox executor)", Kind::Cheat, Match::Contains, Roblox},
    {"krampus", "Krampus (Roblox executor)", Kind::Cheat, Match::Name, Roblox},
    {"macsploit", "MacSploit (Roblox executor)", Kind::Cheat, Match::Contains, Roblox},
    {"ronix", "Ronix (Roblox executor)", Kind::Cheat, Match::Name, Roblox},
    {"celery", "Celery (Roblox executor)", Kind::Cheat, Match::ExeOnly, Roblox},
    {"wave", "Wave (Roblox executor)", Kind::Cheat, Match::ExeOnly, Roblox},
    {"xeno", "Xeno (Roblox executor)", Kind::Cheat, Match::ExeOnly, Roblox},
    {"codex", "Codex (Roblox executor)", Kind::Cheat, Match::ExeOnly, Roblox},
    {"hydrogen", "Hydrogen (Roblox executor)", Kind::Cheat, Match::ExeOnly, Roblox},
    {"evon", "Evon (Roblox executor)", Kind::Cheat, Match::ExeOnly, Roblox},
    {"trigon", "Trigon (Roblox executor)", Kind::Cheat, Match::ExeOnly, Roblox},
    {"volcano", "Volcano (Roblox executor)", Kind::Cheat, Match::ExeOnly, Roblox},
    {"potassium", "Potassium (Roblox executor)", Kind::Cheat, Match::ExeOnly, Roblox},

    // Counter-Strike 2 cheats.
    {"aimware", "Aimware (CS2 cheat)", Kind::Cheat, Match::Contains, CS2},
    {"onetap", "Onetap (CS2 cheat)", Kind::Cheat, Match::Contains, CS2},
    {"neverlose", "Neverlose (CS2 cheat)", Kind::Cheat, Match::Contains, CS2},
    {"gamesense", "Gamesense / skeet (CS2 cheat)", Kind::Cheat, Match::Contains, CS2},
    {"skeet", "Gamesense / skeet (CS2 cheat)", Kind::Cheat, Match::ExeOnly, CS2},
    {"fatality.win", "Fatality (CS2 cheat)", Kind::Cheat, Match::Contains, CS2},
    {"memesense", "Memesense (CS2 cheat)", Kind::Cheat, Match::Contains, CS2},
    {"nixware", "Nixware (CS2 cheat)", Kind::Cheat, Match::Contains, CS2},
    {"interium", "Interium (CS2 cheat)", Kind::Cheat, Match::Contains, CS2},
    {"midnight.im", "Midnight (CS2 cheat)", Kind::Cheat, Match::Contains, CS2},
    {"primordial", "Primordial (CS2 cheat)", Kind::Cheat, Match::ExeOnly, CS2},
    {"plaguecheat", "Plaguecheat (CS2 cheat)", Kind::Cheat, Match::Contains, CS2},
    {"ekknod", "ekknod (CS2 cheat)", Kind::Cheat, Match::Contains, CS2},
    {"legendware", "Legendware (CS2 cheat)", Kind::Cheat, Match::Contains, CS2},
    {"ev0lve", "ev0lve (CS2 cheat)", Kind::Cheat, Match::Contains, CS2},
    {"osiris", "Osiris (CS2 cheat)", Kind::Cheat, Match::ExeOnly, CS2},
    {"cs2 cheat", "CS2 cheat", Kind::Cheat, Match::Contains, CS2},
    {"cs2cheat", "CS2 cheat", Kind::Cheat, Match::Contains, CS2},
    {"cs2-external", "CS2 external cheat", Kind::Cheat, Match::Contains, CS2},
    {"cs2_external", "CS2 external cheat", Kind::Cheat, Match::Contains, CS2},
    {"cs2-internal", "CS2 internal cheat", Kind::Cheat, Match::Contains, CS2},

    // Easy Anti-Cheat games.
    {"eac bypass", "EAC bypass", Kind::Cheat, Match::Contains, Ezac},
    {"eacbypass", "EAC bypass", Kind::Cheat, Match::Contains, Ezac},
    {"eac_bypass", "EAC bypass", Kind::Cheat, Match::Contains, Ezac},
    {"eac-bypass", "EAC bypass", Kind::Cheat, Match::Contains, Ezac},
    {"eac spoofer", "EAC HWID spoofer", Kind::Cheat, Match::Contains, Ezac},
    {"hwid spoofer", "HWID spoofer (ban evasion)", Kind::Cheat, Match::Contains, Ezac | CS2},
    {"hwidspoofer", "HWID spoofer (ban evasion)", Kind::Cheat, Match::Contains, Ezac | CS2},

    // Cheat tools, for every game.
    {"cheat engine", "Cheat Engine (memory editor)", Kind::Tool, Match::Contains, AnyGame},
    {"cheatengine", "Cheat Engine (memory editor)", Kind::Tool, Match::Contains, AnyGame},
    {"extreme injector", "Extreme Injector (DLL injector)", Kind::Tool, Match::Contains, AnyGame},
    {"xenos", "Xenos (DLL injector)", Kind::Tool, Match::ExeOnly, AnyGame},
    {"xenos64", "Xenos (DLL injector)", Kind::Tool, Match::ExeOnly, AnyGame},
    {"gh injector", "GH Injector (DLL injector)", Kind::Tool, Match::Contains, AnyGame},
    {"ghinjector", "GH Injector (DLL injector)", Kind::Tool, Match::Contains, AnyGame},
    {"gh_injector", "GH Injector (DLL injector)", Kind::Tool, Match::Contains, AnyGame},
    {"dll injector", "DLL injector", Kind::Tool, Match::Contains, AnyGame},
    {"dllinjector", "DLL injector", Kind::Tool, Match::Contains, AnyGame},
    {"kdmapper", "kdmapper (loads unsigned drivers)", Kind::Tool, Match::Contains, AnyGame},
    {"kdu", "KDU (loads unsigned drivers)", Kind::Tool, Match::ExeOnly, AnyGame},
    {"reclass.net", "ReClass.NET (memory structure tool)", Kind::Tool, Match::Contains, AnyGame},
    {"reclass", "ReClass (memory structure tool)", Kind::Tool, Match::ExeOnly, AnyGame},
    {"ksdumper", "KsDumper (game memory dumper)", Kind::Tool, Match::Contains, AnyGame},
    {"titanhide", "TitanHide (hides debuggers from anti-cheat)", Kind::Tool, Match::Contains, AnyGame},
    {"scyllahide", "ScyllaHide (hides debuggers from anti-cheat)", Kind::Tool, Match::Contains, AnyGame},
};

// Drivers that cheats and cheat tools load (by file name, lower case).
struct DriverSignature {
    const char *file;
    const char *label;
};
inline const DriverSignature kDrivers[] = {
    {"iqvw64e.sys", "Intel diagnostic driver used by kdmapper to load cheat drivers"},
    {"dbk64.sys", "Cheat Engine's kernel driver"},
    {"dbk32.sys", "Cheat Engine's kernel driver"},
    {"capcom.sys", "Capcom driver (runs any code in the kernel; used by cheats)"},
    {"kprocesshacker.sys", "old Process Hacker driver (used to tamper with anti-cheat)"},
};

// DLLs Windows looks for in a program's own folder first: a copy there runs
// inside the game (how many cheats get in; ReShade and some mods use it too).
inline const char *const kProxyDlls[] = {
    "dxgi.dll",   "d3d9.dll",     "d3d10.dll",     "d3d11.dll", "d3d12.dll",     "dinput8.dll",
    "version.dll", "winmm.dll",   "winhttp.dll",   "wininet.dll", "xinput1_3.dll", "xinput1_4.dll",
    "opengl32.dll", "dsound.dll", "hid.dll",       "dbghelp.dll",
};
