#pragma once

#include <asp/fs/Path.hpp>
#include <cstdint>
#include <string>
#include <vector>
class PlayLayer;
#include "Replay.hpp"

// Click Before Frames (replay based).
// Pick a macro made with the Replay Bot. When you click up to N ticks before
// one of its clicks (same kind: press or release), your click is held back so
// it acts on exactly the replay's tick. Other clicks go through unchanged.
namespace cmm::cbf {
    // Stages like a gear: 0 = off, then 2 / 4 / 8 / 16 (wave 8) / 32 / 64 / 96 / 128 ticks.
    int stage();
    int stageCount();
    void setStage(int stage);
    void cycleStage();
    std::string stageText(int stage);
    int limitFor(bool wave);
    // Max delay for normal gamemodes at the current stage.
    int windowTicks();

    // The macro CBF follows ("" = none). Shares the Replay Bot's macros folder.
    std::string const& replayName();
    // The level the replay was recorded on ("" when it wasn't saved).
    std::string const& replayLevel();
    // False when the replay was recorded on another level than this one.
    // (Replays without a saved level can't be checked and count as usable.)
    bool replayUsable(PlayLayer* pl);
    // The clicks of that replay, sorted by tick.
    std::vector<replay::Input> const& replayInputs();
    // GD progress units per physics tick (normally 2).
    unsigned progressPerTick();
    // True while CBF gives a held-back click to GD (at the start of a tick).
    bool injecting();
    bool setReplay(std::string const& name);
    void reloadReplay();
    int replayClicks();

    // Physics steps counted since the game started (always counting).
    int64_t currentTick();
    int savedThisSession();
    int savedThisAttempt();
    asp::fs::Path logPath();

    // Last snap, for the self-test.
    struct Decision {
        int64_t tick = -1;
        bool down = false;
        int delay = -1;          // -1 = no replay click nearby, went through as is
        int64_t yourTick = 0;    // in physics ticks of the attempt
        int64_t replayTick = 0;
    };
    Decision const& lastDecision();
}
