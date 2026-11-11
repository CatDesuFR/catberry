#pragma once

#include <cstddef>

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

    // ImGui tally, drawn with the menu overlay.
    void drawOverlay();
}
