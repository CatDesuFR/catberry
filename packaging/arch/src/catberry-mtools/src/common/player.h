// Plays a recorded macro on its own thread.
#pragma once

#include "macro.h"
#include "output.h"

#include <atomic>
#include <functional>
#include <thread>

class Player {
public:
    // `onDone` runs on the playback thread when playback ends by itself.
    Player(Output &output, std::function<void()> onDone) : output_(output), onDone_(std::move(onDone)) {}
    ~Player() { stop(); }

    bool playing() const { return playing_; }
    // repeat == 0 plays until stopped.
    void start(const Macro &macro, int repeat);
    void stop();

private:
    void run(const Macro &macro, int repeat);

    Output &output_;
    std::function<void()> onDone_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> playing_{false};
    std::thread thread_;
};
