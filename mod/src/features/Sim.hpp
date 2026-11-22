#pragma once

#include <Geode/Geode.hpp>

// Runs a hidden copy of a player forward in time without touching the real
// level. Used by Click Before Frames (to test a click before letting it
// through) and by Prediction.
//
// While a step runs, anything that would change the real level (deaths,
// triggers, pickups, orb "used" flags...) is blocked or put back.
namespace cmm::sim {
    // Hidden players owned by this module, recreated per PlayLayer.
    PlayerObject* scratch(PlayLayer* pl, int slot);

    // Make `dst` an exact copy of `src`'s physics state.
    void copyState(PlayerObject* dst, PlayerObject* src);

    // One physics tick. Returns false if the player died, or reached
    // something we can't simulate (portal to another gamemode, level end...).
    bool step(PlayLayer* pl, PlayerObject* p);
    // Part of a tick (0..1), for clicks that land between ticks.
    bool stepPart(PlayLayer* pl, PlayerObject* p, float fraction);

    // Put back orb/pad flags touched since the last call. Call after a run.
    void restoreTouched();

    // True for the hidden players this module owns.
    bool isScratch(cocos2d::CCObject* obj);

    bool simulating();
    bool stopped();  // the last run hit something we can't simulate
    void clearStopped();
    float stepDt();  // dt GD passes per physics tick

    // Engine replays: the real level, rewound to a practice checkpoint and
    // run by the game itself (the Frame Window Counter's loading screen).
    // While one runs, a death is only noted (no death, no respawn) and the
    // level end only stops it.
    void beginEngine();
    void endEngine();
    bool engineRunning();
    bool engineDied();   // the player died in this replay
    bool engineEnded();  // the level ended in this replay
}
