#pragma once

#include <set>

// Small shared counters used by hacks and labels.
namespace cmm::stats {
    inline bool noclipHitThisFrame = false;
    inline bool noclipHitLastFrame = false;
    inline int noclipDeaths = 0;
    inline int noclipDeadFrames = 0;
    inline int attemptFrames = 0;
    // Set while a hack kills the player on purpose, so noclip lets it through.
    inline bool forceKill = false;
    // Keys held right now (cocos key codes), for the Keys label.
    inline std::set<int> heldKeys;

    inline void resetAttempt() {
        noclipHitThisFrame = false;
        noclipHitLastFrame = false;
        noclipDeaths = 0;
        noclipDeadFrames = 0;
        attemptFrames = 0;
    }

    inline float noclipAccuracy() {
        if (attemptFrames == 0) return 100.f;
        return 100.f * (1.f - static_cast<float>(noclipDeadFrames) / static_cast<float>(attemptFrames));
    }
}
