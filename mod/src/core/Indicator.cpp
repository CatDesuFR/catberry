#include "Indicator.hpp"
#include "Log.hpp"
#include <fmt/ranges.h>

namespace cmm::indicator {
    static PC s_last = PC::Allowed;
    static bool s_used = false;
    static std::vector<std::string> s_reasons;

    static bool isActive(Hack const& hack) {
        if (!hack.enabled) return false;
        if (hack.active) return hack.active();
        return true;
    }

    PC current() {
        for (auto const& hack : all()) {
            if (hack.pc == PC::NotAllowed && isActive(hack)) return PC::NotAllowed;
        }
        return PC::Allowed;
    }

    PC attempt() {
        return current();
    }

    bool usedThisAttempt() {
        return s_used || current() == PC::NotAllowed;
    }

    std::vector<std::string> const& reasons() {
        return s_reasons;
    }

    void onAttemptStart() {
        s_last = PC::Allowed;
        s_used = false;
        s_reasons.clear();
    }

    void onFrame() {
        auto now = current();
        if (now == PC::NotAllowed) s_used = true;
        if (now == s_last) return;
        s_last = now;
        s_reasons.clear();
        if (now == PC::NotAllowed) {
            for (auto const& hack : all()) {
                if (hack.pc == PC::NotAllowed && isActive(hack)) s_reasons.push_back(hack.name);
            }
            log::warn("Cheat indicator turned red ({})", fmt::join(s_reasons, ", "));
        }
    }
}
