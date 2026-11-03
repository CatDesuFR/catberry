// KDE's Shake Cursor effect enlarges the pointer when it moves fast, which a
// quick click's jumps look like. It is switched off just for a run, and only
// when it was on. Does nothing on other desktops and on Windows.
#pragma once

class ShakeCursorPause {
public:
    ShakeCursorPause(); // restores the effect if an earlier run left it off
    ~ShakeCursorPause();
    void pause();  // before a run
    void resume(); // some time after it
    bool paused() const { return paused_; }

private:
    bool paused_ = false;
};
