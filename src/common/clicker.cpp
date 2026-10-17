#include "clicker.h"

#include "keys.h"
#include "platform.h"

#include <algorithm>

// Apps that poll the mouse once per frame miss a press that is released in
// the same instant, so each click is held for half its interval, up to this.
static const auto kMaxHold = std::chrono::milliseconds(20);

Clicker::Clicker(Output &output) : output_(output), code_(keycode::MouseLeft), thread_([this] { loop(); }) {}

Clicker::~Clicker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

void Clicker::setRunning(bool on) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = on;
    }
    wake_.notify_all();
}

void Clicker::setCps(double cps) { cps_ = std::clamp(cps, kMinCps, kMaxCps); }

void Clicker::loop() {
    auto stopped = [this] { return quit_ || !running_; };
    std::unique_lock<std::mutex> lock(mutex_);
    while (!quit_) {
        wake_.wait(lock, [this] { return quit_ || running_; });
        lock.unlock();
        auto next = Clock::now();
        while (!stopped()) {
            auto interval =
                std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / cps_));
            int code = code_;
            output_.key(code, true);
            // Stopping cuts the hold short, but the release is always sent.
            sleepUntil(next + std::min<Clock::duration>(interval / 2, kMaxHold), stopped);
            output_.key(code, false);
            next += interval;
            // Small wake-up delays are made up on the next clicks; only after
            // falling a whole click behind are the missed clicks skipped
            // instead of sent in a burst.
            auto now = Clock::now();
            if (now - next > interval) next = now;
            sleepUntil(next, stopped);
        }
        lock.lock();
    }
}
