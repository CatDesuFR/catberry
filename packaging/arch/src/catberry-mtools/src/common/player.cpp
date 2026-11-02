#include "player.h"

#include "platform.h"

#include <set>

void Player::start(const Macro &macro, int repeat) {
    stop();
    stop_ = false;
    playing_ = true;
    thread_ = std::thread([this, macro, repeat] { run(macro, repeat); });
}

void Player::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

void Player::run(const Macro &macro, int repeat) {
    std::set<int> held;
    auto stopped = [this] { return stop_.load(); };
    for (int i = 0; (repeat == 0 || i < repeat) && !stop_; ++i) {
        auto begin = Clock::now();
        for (const MacroEvent &ev : macro.events) {
            sleepUntil(begin + std::chrono::microseconds(ev.us), stopped);
            if (stop_) break;
            output_.key(ev.code, ev.down);
            if (ev.down)
                held.insert(ev.code);
            else
                held.erase(ev.code);
        }
        sleepUntil(begin + std::chrono::microseconds(macro.durationUs), stopped);
    }
    // Never leave a key stuck down when interrupted mid-macro.
    for (int code : held) output_.key(code, false);
    bool byItself = !stop_;
    playing_ = false;
    if (byItself && onDone_) onDone_();
}
