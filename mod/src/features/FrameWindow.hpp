#pragma once

#include <features/Replay.hpp>

#include <cstddef>
#include <string>

// Frame Window analyser and counter.
//
// For each click of the replay Click Before Frames follows, the hidden player
// (Sim.cpp) is run from your real state with that click moved earlier and
// later, tick by tick. The click's "frame window" is how many ticks in a row
// it could have landed on and still survived until just before the next click.
//
// - Click Before Frames uses the earliest of those ticks as its target.
// - The counter draws a ring and the window at each click, in NaN GD's
//   colours, and keeps a tally per colour band.
namespace cmm::fw {
    // Earliest GD progress value (m_currentProgress) the click at this replay
    // index could act on and survive. False if it isn't analysed yet.
    bool earliestFor(size_t replayIndex, unsigned& progress);

    // True while the loading screen is measuring the bot's macro.
    bool loading();

    // The Replay Bot just put the player back where the recording had it (a
    // fix with no click, at GD progress `progress`): re-runs do the same.
    void noteBotFix(replay::Fix const& fix, unsigned progress);

    // Developer tally per game mode (presses, measured, "?", over 10, dropped
    // and why), for tests; reset with resetDebugStats().
    std::string debugStats();
    void resetDebugStats();

    // ImGui tally, drawn with the menu overlay.
    void drawOverlay();
}
