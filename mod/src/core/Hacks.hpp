#pragma once

#include <Geode/Geode.hpp>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cmm {

    // Status of a hack on Pointercrate's "Allowed Mods/Programs" sheet (Eclipse v1.9.4 tab).
    // Anything not on that sheet counts as NotAllowed, as the sheet says.
    enum class PC { Allowed = 0, Sometimes = 1, NotAllowed = 2 };

    enum class Tab { Global, Player, Level, Bypass, Creator, Labels, CBF, Menu, Shortcuts, Bot, Performance };
    char const* tabName(Tab tab);

    struct Hack {
        std::string id;
        std::string name;
        std::string desc;
        Tab tab;
        PC pc;
        bool enabled = false;
        // Draws extra options in the hack's popup. Empty = no options.
        std::function<void()> options;
        // Optional: whether the hack is currently changing gameplay (e.g. speedhack at 1x is not).
        std::function<bool()> active;
        // Called after the user toggles the hack.
        std::function<void(bool)> onToggle;
        // If set, this is a one-shot button (Shortcuts tab) instead of a toggle.
        std::function<void()> action;
    };

    Hack& add(Hack hack);
    std::deque<Hack>& all();
    Hack* find(std::string_view id);
    bool on(std::string_view id);
    void setEnabled(Hack& hack, bool enabled);
    void loadHacks();
    void registerHacks();

    struct StringHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };

    // Simple cached settings saved with the mod's saved values.
    // Lookups by string literal don't allocate.
    template <class T>
    T& val(std::string_view key, T def) {
        static std::unordered_map<std::string, T, StringHash, std::equal_to<>> cache;
        auto it = cache.find(key);
        if (it == cache.end()) {
            it = cache.emplace(std::string(key), geode::Mod::get()->getSavedValue<T>(key, def)).first;
        }
        return it->second;
    }

    template <class T>
    void save(std::string_view key, T const& value) {
        val<T>(key, value) = value;
        geode::Mod::get()->setSavedValue<T>(key, value);
    }

    // Draw helpers for options popups that save on change.
    bool optFloat(char const* label, std::string_view key, float def, float min, float max, char const* fmt = "%.2f");
    bool optInt(char const* label, std::string_view key, int def, int min, int max);
    bool optBool(char const* label, std::string_view key, bool def);
}
