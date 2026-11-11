#include "Hacks.hpp"
#include "Log.hpp"
#include <imgui.h>

namespace cmm {
    char const* tabName(Tab tab) {
        switch (tab) {
            case Tab::Global: return "Global";
            case Tab::Player: return "Player";
            case Tab::Level: return "Level";
            case Tab::Bypass: return "Bypass";
            case Tab::Creator: return "Creator";
            case Tab::Labels: return "Labels";
            case Tab::CBF: return "Click Before Frames";
            case Tab::Menu: return "Catberry's Mod Menu";
            case Tab::Shortcuts: return "Shortcuts";
            case Tab::Bot: return "Replay Bot";
            case Tab::Performance: return "Performance";
        }
        return "?";
    }

    std::deque<Hack>& all() {
        static std::deque<Hack> hacks;
        return hacks;
    }

    // Transparent hashing so lookups with a string literal don't allocate.
    static std::unordered_map<std::string, Hack*, StringHash, std::equal_to<>>& index() {
        static std::unordered_map<std::string, Hack*, StringHash, std::equal_to<>> map;
        return map;
    }

    Hack& add(Hack hack) {
        auto& ref = all().emplace_back(std::move(hack));
        index()[ref.id] = &ref;
        return ref;
    }

    Hack* find(std::string_view id) {
        auto it = index().find(id);
        return it == index().end() ? nullptr : it->second;
    }

    bool on(std::string_view id) {
        auto it = index().find(id);
        return it != index().end() && it->second->enabled;
    }

    void setEnabled(Hack& hack, bool enabled) {
        if (hack.enabled == enabled) return;
        hack.enabled = enabled;
        geode::Mod::get()->setSavedValue<bool>("hack." + hack.id, enabled);
        log::info("{} {}", enabled ? "Enabled" : "Disabled", hack.name);
        if (hack.onToggle) hack.onToggle(enabled);
    }

    void loadHacks() {
        for (auto& hack : all()) {
            if (hack.action) continue;
            hack.enabled = geode::Mod::get()->getSavedValue<bool>("hack." + hack.id, false);
            if (hack.enabled && hack.onToggle) hack.onToggle(true);
        }
    }

    bool optFloat(char const* label, std::string_view key, float def, float min, float max, char const* fmt) {
        auto& v = val<float>(key, def);
        if (ImGui::SliderFloat(label, &v, min, max, fmt)) {
            save<float>(key, v);
            return true;
        }
        return false;
    }

    bool optInt(char const* label, std::string_view key, int def, int min, int max) {
        auto& v = val<int>(key, def);
        if (ImGui::SliderInt(label, &v, min, max)) {
            save<int>(key, v);
            return true;
        }
        return false;
    }

    bool optBool(char const* label, std::string_view key, bool def) {
        auto& v = val<bool>(key, def);
        if (ImGui::Checkbox(label, &v)) {
            save<bool>(key, v);
            return true;
        }
        return false;
    }
}
