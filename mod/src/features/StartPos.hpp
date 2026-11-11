#pragma once

class PlayLayer;

namespace cmm::startpos {
    // -1 = start of the level, otherwise index into the sorted start positions.
    int currentIndex();
    int count();
    void step(PlayLayer* pl, int direction);
}
