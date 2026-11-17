#pragma once

#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/binding/PlayLayer.hpp>

// In a one-player level GD's second jump key (the "player 2" key) also
// controls player 1, so its clicks count as player 1's. Only a real
// two-player level keeps the two apart.
namespace cmm {
    inline bool twoPlayer() {
        auto pl = PlayLayer::get();
        return pl && pl->m_levelSettings && pl->m_levelSettings->m_twoPlayerMode;
    }
    // Whether a click marked player 2 really is player 2's.
    inline bool forPlayer2(bool markedPlayer2) { return markedPlayer2 && twoPlayer(); }
}
