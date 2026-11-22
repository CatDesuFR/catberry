// Frame Window analyser and counter. See FrameWindow.hpp.
//
// Timing (measured in game, see ClickBeforeFrames.cpp): a click that came in
// at progress P (between frames, after tick T) acts on tick T + 2. From the
// player's state after tick "now", run tick k of the hidden player is real
// tick now + 1 + k, so that click acts on run tick (T - now) + 1.
#include "FrameWindow.hpp"
#include "ClickBeforeFrames.hpp"
#include "Sim.hpp"
#include "WorldOverlay.hpp"
#include <core/Hacks.hpp>
#include <core/Players.hpp>
#include <core/Log.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <deque>
#include <map>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/EnhancedGameObject.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <features/Replay.hpp>
#include <cstring>
#include <Geode/fmod/fmod.hpp>
#include <fmt/ranges.h>

using namespace geode::prelude;

namespace cmm::fw {
    namespace {
        struct Result {
            bool analysed = false;
            int window = 0;     // ticks in a row that survive (0 = not even the original)
            int earliest = 0;   // offset of the earliest surviving tick (<= 0)
            int latest = 0;
            bool passed = false;   // the player reached this click
            CCPoint where;
        };

        std::vector<Result> s_results;
        size_t s_replaySize = 0;

        // Windows of the replay's clicks are only needed by Click Before Frames.
        bool analyserOn() { return cmm::on("prediction"); }
        int searchRange() { return std::clamp(cmm::val<int>("prediction.range", 16), 2, 64); }

        // NaN GD's bands, by the exact window in frames (240 per second):
        // 0-1, 1-2, 2-3, 3-4, 4-5, 5-6, 6-8, 8-10, then over 10.
        constexpr int kBands = 9;
        char const* kBandNames[kBands] = {"0-1", "1-2", "2-3", "3-4", "4-5", "5-6", "6-8", "8-10", "10+"};
        constexpr ccColor3B kBandColors[kBands] = {
            {255, 40, 40}, {255, 90, 140}, {255, 140, 0}, {255, 225, 0}, {120, 235, 60},
            {0, 230, 200}, {30, 170, 255}, {110, 100, 255}, {190, 90, 255}};
        int bandOf(double window) {
            if (window < 6.0) return std::clamp(static_cast<int>(std::floor(window)), 0, 5);
            if (window < 8.0) return 6;
            if (window <= 10.0) return 7;
            return 8;
        }
        ccColor3B bandColor(double window) { return kBandColors[bandOf(window)]; }

        // ---- precision (NaNDL L*, as NaN GD shows it) ----
        // Each measured click i has a window of w_i ticks (240 per second) and
        // happens t_i seconds into the attempt (plus the respawn time). With
        // precision L (sigma per second), it lands in its window with
        // p_i = erf(w_i / 240 * L / (2 * sqrt 2)). The expected time to beat
        // the level, counting every failed attempt up to where it failed:
        //   E[T_A] = t_n * P + sum_i t_i * r_i * (1 - p_i),  r_i = prod_{j<i} p_j,
        //   P = prod p_i,  E[T_C] = E[T_A] / P.
        // L* is the precision that makes E[T_C] 24 hours: it goes up with
        // every click (each one makes passing a bit less likely).
        struct PrecisionClick { double window; double time; };
        double expectedTime(std::vector<PrecisionClick> const& clicks, double L) {
            double r = 1.0, failTime = 0.0;
            for (auto const& c : clicks) {
                double x = c.window / 240.0 * L / (2.0 * std::sqrt(2.0));
                if (x > 8.5) continue;            // can't miss
                double q = std::erfc(x);
                double p = std::max(1.0 - q, 1e-300);
                failTime += c.time * r * q;
                r *= p;
            }
            double last = clicks.empty() ? 0.0 : clicks.back().time;
            return (last * r + failTime) / std::max(r, 1e-300);
        }
        double precisionL(std::vector<PrecisionClick> const& clicks) {
            if (clicks.empty()) return 0.0;
            constexpr double kTarget = 24.0 * 3600.0;
            if (clicks.back().time >= kTarget) return 1e9;
            double lo = 1e-6, hi = 1.0;
            while (expectedTime(clicks, hi) > kTarget && hi < 1e9) { lo = hi; hi *= 2.0; }
            for (int i = 0; i < 80; i++) {        // E[T_C] goes down as L goes up
                double mid = std::sqrt(lo * hi);
                if (expectedTime(clicks, mid) > kTarget) lo = mid;
                else hi = mid;
            }
            return hi;
        }

        // ---- dings ----
        // A short bell, made here so no sound files are needed. Tighter
        // windows ring higher. One sound per band, created once.
        FMOD::Sound* s_dings[kBands] = {};
        std::vector<char> s_dingData[kBands];

        std::vector<char> makeBell(float freq) {
            constexpr int rate = 44100;
            int samples = rate * 45 / 100;  // 0.45 s
            std::vector<char> wav(44 + samples * 2);
            auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; i++) wav[at + i] = static_cast<char>(v >> (8 * i)); };
            auto put16 = [&](size_t at, uint16_t v) { wav[at] = static_cast<char>(v); wav[at + 1] = static_cast<char>(v >> 8); };
            std::memcpy(wav.data(), "RIFF", 4); put32(4, 36 + samples * 2);
            std::memcpy(wav.data() + 8, "WAVEfmt ", 8); put32(16, 16); put16(20, 1); put16(22, 1);
            put32(24, rate); put32(28, rate * 2); put16(32, 2); put16(34, 16);
            std::memcpy(wav.data() + 36, "data", 4); put32(40, samples * 2);
            for (int i = 0; i < samples; i++) {
                float t = static_cast<float>(i) / rate;
                float attack = std::min(1.f, t / 0.004f);
                // A bell: the main tone plus two quieter, inharmonic overtones.
                float v = std::sin(2.f * static_cast<float>(M_PI) * freq * t) * std::exp(-t * 7.f)
                        + 0.45f * std::sin(2.f * static_cast<float>(M_PI) * freq * 2.76f * t) * std::exp(-t * 11.f)
                        + 0.2f * std::sin(2.f * static_cast<float>(M_PI) * freq * 5.4f * t) * std::exp(-t * 16.f);
                auto sample = static_cast<int16_t>(std::clamp(v * attack * 0.45f, -1.f, 1.f) * 32767.f);
                put16(44 + i * 2, static_cast<uint16_t>(sample));
            }
            return wav;
        }

        void ding(double window) {
            if (!cmm::val<bool>("framecounter.sound", true)) return;
            auto engine = FMODAudioEngine::sharedEngine();
            if (!engine || !engine->m_system) return;
            int band = bandOf(window);
            // band 0 = under 1 frame (highest) ... 8 = over 10 (lowest)
            constexpr float kFreq[kBands] = {1760.f, 1568.f, 1397.f, 1245.f, 1109.f, 988.f, 880.f, 784.f, 659.f};
            if (!s_dings[band]) {
                s_dingData[band] = makeBell(kFreq[band]);
                FMOD_CREATESOUNDEXINFO info{};
                info.cbsize = sizeof(info);
                info.length = static_cast<unsigned>(s_dingData[band].size());
                auto result = engine->m_system->createSound(s_dingData[band].data(), FMOD_OPENMEMORY | FMOD_CREATESAMPLE, &info, &s_dings[band]);
                if (result != FMOD_OK) {
                    log::warn("Frame window ding: could not create the sound (FMOD error {})", static_cast<int>(result));
                    s_dings[band] = nullptr;
                    return;
                }
                log::debug("Frame window ding: sound for band {} ready ({:.0f} Hz)", band, kFreq[band]);
            }
            FMOD::Channel* channel = nullptr;
            auto played = engine->m_system->playSound(s_dings[band], nullptr, false, &channel);
            if (played == FMOD_OK && channel) {
                channel->setVolume(std::clamp(cmm::val<float>("framecounter.volume", 0.6f), 0.f, 1.f));
            } else {
                log::warn("Frame window ding: could not play (FMOD error {})", static_cast<int>(played));
            }
        }

        bool isP1(replay::Input const& in) { return in.button == 1 && !cmm::forPlayer2(in.player2); }

        // NaN GD shows no marker for windows over 10 (easy clicks); optional.
        bool shown(double window) { return window <= 10.0 || !cmm::val<bool>("framecounter.hide10", true); }

        // Runs the hidden player from the real one with the replay's clicks,
        // click `index` (and its release) moved by `offset` ticks. True if it
        // survives to `horizon`.
        bool leg(PlayLayer* pl, std::vector<replay::Input> const& inputs, size_t index, size_t pairIndex,
                 int offset, unsigned now, unsigned ppt, int horizon) {
            auto p = sim::scratch(pl, 2);
            if (!p) return false;
            sim::copyState(p, pl->m_player1);
            sim::clearStopped();

            // Run tick of every P1 click still ahead.
            struct Event { int k; bool down; };
            std::vector<Event> events;
            for (size_t j = 0; j < inputs.size(); j++) {
                auto const& in = inputs[j];
                if (!isP1(in) || in.tick <= now) continue;
                int k = static_cast<int>((in.tick - now) / ppt) + 1;
                if (j == index || j == pairIndex) k += offset;
                if (k > horizon) continue;
                events.push_back({std::max(0, k), in.down});
            }
            std::stable_sort(events.begin(), events.end(), [](auto const& a, auto const& b) { return a.k < b.k; });

            size_t e = 0;
            bool alive = true;
            for (int k = 0; k < horizon && alive; k++) {
                while (e < events.size() && events[e].k == k) {
                    if (events[e].down) p->pushButton(PlayerButton::Jump);
                    else p->releaseButton(PlayerButton::Jump);
                    e++;
                }
                alive = sim::step(pl, p);
                if (!alive && sim::stopped()) alive = true;  // can't see further: count as alive
                if (sim::stopped()) break;
            }
            sim::restoreTouched();
            return alive;
        }

        void analyse(PlayLayer* pl, std::vector<replay::Input> const& inputs, size_t index, unsigned now, unsigned ppt) {
            auto& r = s_results[index];
            r.analysed = true;
            auto const& in = inputs[index];
            int k = static_cast<int>((in.tick - now) / ppt) + 1;

            // A press moves together with its release (the hold stays the same).
            size_t pairIndex = SIZE_MAX, nextIndex = SIZE_MAX;
            for (size_t j = index + 1; j < inputs.size(); j++) {
                if (!isP1(inputs[j])) continue;
                if (in.down && !inputs[j].down && pairIndex == SIZE_MAX) { pairIndex = j; continue; }
                nextIndex = j;
                break;
            }
            // Survive until a few ticks before the next click (at least 12, at most 240 ticks).
            int horizon = k + 120;
            if (nextIndex != SIZE_MAX) horizon = static_cast<int>((inputs[nextIndex].tick - now) / ppt) + 1 - 4;
            horizon = std::clamp(horizon, k + 12, k + 240);

            int range = searchRange();
            if (!leg(pl, inputs, index, pairIndex, 0, now, ppt, horizon)) {
                r.window = 0;
                r.earliest = r.latest = 0;
                return;
            }
            int latest = 0, earliest = 0;
            while (latest < range && leg(pl, inputs, index, pairIndex, latest + 1, now, ppt, horizon + latest + 1)) latest++;
            int minOffset = -std::min(range, k);  // can't act before run tick 0
            while (earliest > minOffset && leg(pl, inputs, index, pairIndex, earliest - 1, now, ppt, horizon)) earliest--;
            r.earliest = earliest;
            r.latest = latest;
            r.window = latest - earliest + 1;
            log::debug("Frame window: click {} ({}) window {} (from {:+} to {:+} ticks)", index, in.down ? "press" : "release",
                r.window, earliest, latest);
        }
    }

    bool earliestFor(size_t replayIndex, unsigned& progress) {
        if (!cmm::on("prediction") || replayIndex >= s_results.size() || !s_results[replayIndex].analysed) return false;
        auto const& r = s_results[replayIndex];
        if (r.window == 0) return false;
        auto const& inputs = cbf::replayInputs();
        if (replayIndex >= inputs.size()) return false;
        long long p = static_cast<long long>(inputs[replayIndex].tick) + static_cast<long long>(r.earliest) * cbf::progressPerTick();
        progress = static_cast<unsigned>(std::max(0LL, p));
        return true;
    }

    // The tally is drawn on the level now (see the list in FrameWindowLayer).
    void drawOverlay() {}
}

// ---------------------------------------------------------------------------
// The counter: windows of the clicks that actually happen (yours, the ones
// Click Before Frames hands to GD, and the Replay Bot's).
//
// The player's state is kept for the last ticks. When a press comes in, the
// state from `range` ticks before it is kept. Once the next press comes (so we
// know how long it had to survive), that stretch is re-run on the hidden
// player with the press (and its release) moved earlier and later, with every
// other click where it really was. The window is how many ticks in a row
// survive until just before that next press. If even the press where it
// really was doesn't survive in the re-run, the result is "?" (the simulation
// can't reproduce this spot), never a made-up number.
//
// Ticks: a click made between frames after tick T acts on tick T + 2; one
// handed to GD at the start of a tick acts on that tick (measured in game).
// ---------------------------------------------------------------------------
namespace cmm::fw::own {
    namespace {
        constexpr int kLegSlot = 3;
        // Clicks waiting to be measured: plenty, so spam sections at a big
        // range don't run out (a click without a slot can't be measured).
        constexpr int kPendingSlots = 64;  // sim slots 4..67
        constexpr int kRingBase = 68;      // sim slots 68..139
        constexpr int kRing = 72;          // kept states of history, kStride() ticks apart

        int64_t s_tick = 0;                // physics ticks, counted here
        unsigned s_lastProgress = ~0u;
        bool s_inStep = false;
        int64_t s_ringTick[kRing];
        bool s_ringReady = false;

        // fix: the bot's position fix for this click (bot clicks only)
        struct Click { int64_t act; bool down; replay::Fix fix = {}; };
        std::deque<Click> s_clicks;        // recent clicks of player 1
        bool s_held = false;               // player 1's jump button is down
        // The bot's regular fixes (no click): put back at the start of `tick`.
        struct TimedFix { int64_t tick; replay::Fix fix; };
        std::deque<TimedFix> s_botFixes;

        struct Activation { Ref<EnhancedGameObject> obj; int64_t tick; };
        std::deque<Activation> s_activations;  // orbs/pads the real player may have used

        int s_nextId = 1;
        struct Pressed { int id; CCPoint where; bool release; };
        std::vector<Pressed> s_pressed;    // new presses: get a placeholder marker

        struct Job {
            int id = 0;
            bool isRelease = false; // measures a release instead of a press
            int64_t act;            // tick the click acted on
            int64_t release = -1;   // for a press: tick its release acted on (moves with it)
            int64_t pressAt = -1;   // for a release: its press (the release can't move before it)
            int64_t base;           // tick the kept state is from (start of that tick)
            int64_t horizon = -1;   // must survive until this tick; -1 = not known yet
            int slot;               // sim slot holding the kept state
            int nearSlot = -1;      // a kept state just before the click (for later moves), or -1
            int64_t nearBase = -1;  // tick of that state
            int gHi = 0;            // doubling search: first offset found that dies (0 = none yet)
            CCPoint where;
            // 0 = as it was, 1 = later, 2 = earlier, 3 = sub-tick at the late
            // edge, 4 = sub-tick at the early edge, 5 = done
            int phase = 0;
            int latest = 0, earliest = 0;
            double right = 0.0, left = 0.0;  // exact edges (in ticks) of the window
            double lo = 0.0, hi = 0.0;       // bisection: lo survives, hi dies
            int steps = 0;
            bool unknown = false;
            int mode = 0;           // game mode at the click (developer tally)
        };
        std::vector<Job> s_jobs;
        bool s_slotUsed[kPendingSlots] = {};

        // window: in ticks, with sub-tick precision (e.g. 5.3)
        struct OwnResult { int id; CCPoint where; double window; bool unknown; int64_t rel; bool release; };
        int64_t s_attemptStart = 0;        // own tick the attempt started on
        std::vector<OwnResult> s_done;     // finished, waiting to be shown
        std::vector<int> s_dropped;        // presses that won't be measured (their placeholder goes)

        // Counter accuracy: how far each press is moved, each way (32 to 4096
        // ticks). Older settings: accuracy 0 = Balanced (32), 1 = Max (64).
        int range() {
            int r = cmm::val<int>("framecounter.range", 0);
            if (r <= 0) r = cmm::val<int>("framecounter.accuracy", 1) == 0 ? 32 : 64;
            return std::clamp(r, 32, 4096);
        }
        // Up to this many ticks a click is moved one tick at a time; past it
        // the search doubles its step, then narrows down to the exact tick.
        constexpr int kLinear = 64;
        // Ticks between kept states: 1 up to a range of 64, more beyond, so
        // the 72 kept states always reach back further than the range.
        int stride() {
            int r = range();
            return r <= kLinear ? 1 : (r + 2 + kRing - 3) / (kRing - 2);
        }
        int s_ringStride = 1;  // stride the ring was filled with

        // Developer tally per game mode (see debugStats()).
        enum Mode { kCube, kShip, kBall, kUfo, kWave, kRobot, kSpider, kSwing, kModes };
        char const* kModeNames[kModes] = {"cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing"};
        struct ModeStats { int presses = 0, measured = 0, unknown = 0, over10 = 0, noHistory = 0, noSlot = 0, death = 0, reset = 0; };
        ModeStats s_stats[kModes];
        int modeOf(PlayerObject* p) {
            if (!p) return kCube;
            if (p->m_isShip) return kShip;
            if (p->m_isBall) return kBall;
            if (p->m_isBird) return kUfo;
            if (p->m_isDart) return kWave;
            if (p->m_isRobot) return kRobot;
            if (p->m_isSpider) return kSpider;
            if (p->m_isSwing) return kSwing;
            return kCube;
        }
        // The kept state from tick `at` or the last one before it, or -1.
        int ringAtOrBefore(int64_t at, int64_t& tick) {
            int st = s_ringStride;
            int64_t want = at >= 0 ? at / st * st : -1;
            if (want < 0) return -1;
            int slot = static_cast<int>((want / st) % kRing);
            if (!s_ringReady || s_ringTick[slot] != want) return -1;
            tick = want;
            return slot;
        }

        int freeSlots() {
            int n = 0;
            for (bool used : s_slotUsed) n += !used;
            return n;
        }

        int freeSlot() {
            for (int i = 0; i < kPendingSlots; i++) {
                if (!s_slotUsed[i]) { s_slotUsed[i] = true; return 4 + i; }
            }
            return -1;
        }
        void releaseSlot(int slot) {
            if (slot >= 4 && slot < 4 + kPendingSlots) s_slotUsed[slot - 4] = false;
        }

        void clearAll() {
            for (auto& j : s_jobs) { releaseSlot(j.slot); if (j.nearSlot >= 0) releaseSlot(j.nearSlot); }
            s_jobs.clear();
            s_clicks.clear();
            s_botFixes.clear();
            s_activations.clear();
            s_ringReady = false;
            s_held = false;
            for (auto& t : s_ringTick) t = -1;
        }

        // Re-run from the kept state with the click (a press together with its
        // release) moved by `offset` ticks, which can be a fraction: a click
        // between two ticks splits that tick in two. True if the player is
        // still alive at the horizon.
        bool leg(PlayLayer* pl, Job const& job, double offset) {
            // Start from the kept state just before the click when the moved
            // click is still after it (much shorter re-run for big ranges).
            int64_t start = job.base;
            int startSlot = job.slot;
            double movedAt = static_cast<double>(job.act) + std::min(0.0, offset);
            if (job.nearSlot >= 0 && movedAt >= static_cast<double>(job.nearBase)) {
                start = job.nearBase;
                startSlot = job.nearSlot;
            }
            auto p = sim::scratch(pl, kLegSlot);
            auto from = sim::scratch(pl, startSlot);
            if (!p || !from) return false;

            // Orbs and pads the real player used after the kept moment have to
            // be usable again in the re-run.
            struct Saved { EnhancedGameObject* obj; bool p1, p2, active; };
            std::vector<Saved> saved;
            for (auto const& a : s_activations) {
                if (a.tick < start || !a.obj) continue;
                auto o = a.obj.data();
                if (!o->m_activatedByPlayer1 && !o->m_activatedByPlayer2 && !o->m_isActivated) continue;
                saved.push_back({o, o->m_activatedByPlayer1, o->m_activatedByPlayer2, o->m_isActivated});
                o->m_activatedByPlayer1 = o->m_activatedByPlayer2 = false;
                o->m_isActivated = false;
            }

            sim::copyState(p, from);
            sim::clearStopped();
            struct Event { double time; bool down; replay::Fix const* fix; };
            std::vector<Event> events;
            // The bot puts the player back where the recording had it at each
            // of its clicks; do the same for the clicks before the moved one,
            // so the re-run matches the real run up to it. (Not after it: that
            // would undo the move.)
            double movedFrom = static_cast<double>(job.act) + std::min(0.0, offset);
            // A click moved later has to happen, and be survived for a while,
            // too: past the usual horizon it used to be left out, so the
            // player only had to live without it and late moves looked fine.
            int64_t horizon = std::max<int64_t>(job.horizon, static_cast<int64_t>(std::ceil(static_cast<double>(job.act) + std::max(0.0, offset))) + 12);
            std::vector<std::pair<int64_t, replay::Fix const*>> fixes;
            for (auto const& c : s_clicks) {
                double t = static_cast<double>(c.act);
                bool moved = job.isRelease ? (c.act == job.act && !c.down)
                                           : ((c.act == job.act && c.down) || (c.act == job.release && !c.down));
                if (moved) t += offset;
                if (t < static_cast<double>(start) || t > static_cast<double>(horizon)) continue;
                // The bot's fix lands at the end of the click's tick (the start
                // of the next one); only while that's still before the move.
                bool useFix = !moved && c.fix.ok && t + 1.0 <= movedFrom;
                events.push_back({t, c.down, nullptr});
                if (useFix) fixes.push_back({c.act + 1, &c.fix});
            }
            for (auto const& bf : s_botFixes) {
                if (bf.tick > start && static_cast<double>(bf.tick) <= movedFrom && bf.tick < horizon) fixes.push_back({bf.tick, &bf.fix});
            }
            std::stable_sort(events.begin(), events.end(), [](auto const& a, auto const& b) { return a.time < b.time; });

            auto apply = [&](Event const& ev) {
                if (ev.down) p->pushButton(PlayerButton::Jump);
                else p->releaseButton(PlayerButton::Jump);
            };
            bool alive = true;
            size_t e = 0;
            size_t fi = 0;
            std::sort(fixes.begin(), fixes.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
            for (int64_t t = start; t < horizon && alive; t++) {
                // The bot's fixes due at the start of this tick.
                while (fi < fixes.size() && fixes[fi].first <= t) {
                    if (fixes[fi].first == t) {
                        auto const* fx = fixes[fi].second;
                        p->m_position = CCPoint(fx->x, fx->y);
                        p->setPosition(p->m_position);
                        p->setRotation(fx->rotation);
                        p->m_yVelocity = fx->yVelocity;
                        if (p->m_isPlatformer) p->m_platformerXVelocity = fx->xVelocity;
                    }
                    fi++;
                }
                // Clicks on this tick; ones between ticks split it.
                double done = 0.0;
                while (alive && e < events.size() && events[e].time < static_cast<double>(t + 1)) {
                    double frac = std::max(0.0, events[e].time - static_cast<double>(t));
                    if (frac > done + 1e-6) {
                        alive = sim::stepPart(pl, p, static_cast<float>(frac - done));
                        done = frac;
                    }
                    apply(events[e]);
                    e++;
                }
                if (alive && done < 1.0 - 1e-6) alive = sim::stepPart(pl, p, static_cast<float>(1.0 - done));
                if (sim::stopped()) { alive = true; break; }
            }
            sim::restoreTouched();
            for (auto const& sv : saved) {
                sv.obj->m_activatedByPlayer1 = sv.p1;
                sv.obj->m_activatedByPlayer2 = sv.p2;
                sv.obj->m_isActivated = sv.active;
            }
            return alive;
        }

        constexpr int kBisect = 6;  // halvings per edge: 1/64 of a tick

        // Works on the oldest ready job for up to `budgetMs`.
        void work(PlayLayer* pl, float budgetMs) {
            auto start = std::chrono::steady_clock::now();
            auto elapsed = [&] { return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count(); };
            int r = range();
            while (elapsed() < budgetMs) {
                auto it = std::find_if(s_jobs.begin(), s_jobs.end(), [](Job const& j) { return j.horizon >= 0 && j.phase < 5; });
                if (it == s_jobs.end()) return;
                auto& job = *it;
                switch (job.phase) {
                    case 0:
                        if (!leg(pl, job, 0.0)) { job.unknown = true; job.phase = 5; }
                        else job.phase = 1;
                        break;
                    case 1: {  // later: a whole tick at a time, then doubling + narrowing down
                        bool more = false;
                        if (job.latest < r && job.latest < kLinear) {
                            more = leg(pl, job, job.latest + 1);
                            if (more) job.latest++;
                        } else if (job.latest < r) {
                            int cand = job.gHi == 0 ? std::min(r, job.latest * 2) : (job.latest + job.gHi) / 2;
                            if (cand > job.latest && (job.gHi == 0 || job.gHi - job.latest > 1)) {
                                more = true;
                                if (leg(pl, job, cand)) job.latest = cand;
                                else job.gHi = cand;
                            }
                        }
                        if (!more) {
                            job.phase = 2;
                            job.right = job.latest;
                            job.gHi = 0;
                        }
                        break;
                    }
                    case 2: {  // earlier: a whole tick at a time, then doubling + narrowing down
                        // The furthest back the click can go: the kept state, and
                        // for a release, just after its press.
                        int minOff = static_cast<int>(std::max<int64_t>(job.base - job.act + 1, -r));
                        if (job.pressAt >= 0) minOff = std::max(minOff, static_cast<int>(job.pressAt - job.act + 1));
                        bool more = false;
                        if (job.earliest > minOff && -job.earliest < kLinear) {
                            more = leg(pl, job, job.earliest - 1);
                            if (more) job.earliest--;
                        } else if (job.earliest > minOff) {
                            int cand = job.gHi == 0 ? std::max(minOff, job.earliest * 2) : (job.earliest + job.gHi) / 2;
                            if (cand < job.earliest && (job.gHi == 0 || job.earliest - job.gHi > 1)) {
                                more = true;
                                if (leg(pl, job, cand)) job.earliest = cand;
                                else job.gHi = cand;
                            }
                        }
                        if (!more) {
                            job.gHi = 0;
                            job.left = job.earliest;
                            // Late edge: between the last tick that survives and the next.
                            if (job.latest < r) {
                                job.phase = 3;
                                job.lo = job.latest;
                                job.hi = job.latest + 1;
                                job.steps = 0;
                            } else {
                                job.phase = 4;
                            }
                        }
                        break;
                    }
                    case 3: {
                        double mid = (job.lo + job.hi) / 2.0;
                        if (leg(pl, job, mid)) job.lo = mid;
                        else job.hi = mid;
                        if (++job.steps >= kBisect) {
                            job.right = job.lo;
                            job.phase = 4;
                            job.steps = -1;
                        }
                        break;
                    }
                    case 4: {
                        // Early edge: between the tick before the first that survives and it.
                        if (job.steps == -1 || job.steps == 0) {
                            bool canGoEarlier = job.act + job.earliest - 1 >= job.base &&
                                                job.act + job.earliest - 1 > job.pressAt;
                            if (job.earliest <= -r || !canGoEarlier) { job.phase = 5; break; }
                            job.lo = job.earliest;      // survives
                            job.hi = job.earliest - 1;  // dies
                            job.steps = 1;
                        }
                        double mid = (job.lo + job.hi) / 2.0;
                        if (leg(pl, job, mid)) job.lo = mid;
                        else job.hi = mid;
                        if (++job.steps > kBisect) {
                            job.left = job.lo;
                            job.phase = 5;
                        }
                        break;
                    }
                }
                if (job.phase == 5) {
                    // The window: the time from the earliest to the latest
                    // moment the click still works. Whole ticks only would give
                    // latest - earliest + 1; the exact edges add the fraction.
                    double window = job.unknown ? 0.0 : std::max(0.1, job.right - job.left);
                    if (!job.isRelease) {
                        auto& st = s_stats[job.mode];
                        st.measured++;
                        if (job.unknown) st.unknown++;
                        else if (window > 10.0) st.over10++;
                    }
                    s_done.push_back({job.id, job.where, window, job.unknown, job.act - s_attemptStart, job.isRelease});
                    log::debug("Frame window ({} {} at tick {}, horizon +{}): {}", kModeNames[job.mode], job.isRelease ? "release" : "press", job.act, job.horizon - job.act,
                        job.unknown ? std::string("? (can't reproduce)") : fmt::format("{:.2f} (from {:+.2f} to {:+.2f})", window, job.left, job.right));
                    releaseSlot(job.slot);
                    if (job.nearSlot >= 0) releaseSlot(job.nearSlot);
                    s_jobs.erase(it);
                }
            }
        }

        // ---- pre-measuring a macro behind a loading screen ----
        enum class Pre { Off, Loading, Ready };
        Pre s_pre = Pre::Off;
        // (attempt tick * 2 + is release) -> (window, unknown)
        std::map<int64_t, std::pair<double, bool>> s_table;
        int s_preTotal = 0;      // presses in the macro
        int s_preMeasured = 0;
        bool s_preStepping = false;  // still fast-forwarding the level
        bool s_preDied = false;      // the macro died while fast-forwarding
        float s_preDiedAt = 0.f;     // where (percent)
        bool s_preAsking = false;    // waiting for "Play anyway" / "Cancel"
        int s_preUnknown = 0;        // clicks that came out as "?"

        // Already measured behind the loading screen: show it right away.
        bool fromTable(PlayLayer* pl, int64_t act, bool release) {
            if (s_pre != Pre::Ready) return false;
            int64_t rel = act - s_attemptStart;
            for (int64_t d : {0LL, -1LL, 1LL}) {
                auto it = s_table.find((rel + d) * 2 + (release ? 1 : 0));
                if (it == s_table.end()) continue;
                s_done.push_back({s_nextId++, pl->m_player1->getPosition(), it->second.first, it->second.second, rel, release});
                return true;
            }
            return false;
        }

        // Keeps the state from before a click and queues it to be measured.
        void startJob(PlayLayer* pl, int64_t act, bool release) {
            int64_t base = 0;
            int ringSlot = ringAtOrBefore(act - range() - 1, base);
            if (ringSlot < 0 && s_ringReady) {
                // Not that much history (a click near the start of the level):
                // start from the oldest kept state, the click can't go earlier.
                for (int i = 0; i < kRing; i++) {
                    if (s_ringTick[i] >= 0 && s_ringTick[i] < act && (ringSlot < 0 || s_ringTick[i] < base)) {
                        ringSlot = i;
                        base = s_ringTick[i];
                    }
                }
            }
            int mode = modeOf(pl->m_player1);
            if (ringSlot < 0) { if (!release) s_stats[mode].noHistory++; return; }  // no history yet
            int slot = freeSlot();
            if (slot < 0) { if (!release) s_stats[mode].noSlot++; return; }  // too many clicks waiting
            auto from = sim::scratch(pl, kRingBase + ringSlot);
            auto to = sim::scratch(pl, slot);
            if (!from || !to) { releaseSlot(slot); return; }
            sim::copyState(to, from);
            // A second kept state just before the click, for the moves later.
            int nearSlot = -1;
            int64_t nearBase = -1;
            if (s_ringStride > 1 || release) {
                // A release starts from the real state of its own tick: a short
                // tap re-run from further back can miss what really happened
                // and come out as "?". (It can't move before its press anyway.)
                int nearRing = ringAtOrBefore(release ? act : act - 1, nearBase);
                if (nearRing >= 0 && nearBase > base) {
                    nearSlot = freeSlot();
                    auto nf = sim::scratch(pl, kRingBase + nearRing);
                    auto nt = nearSlot >= 0 ? sim::scratch(pl, nearSlot) : nullptr;
                    if (nf && nt) sim::copyState(nt, nf);
                    else if (nearSlot >= 0) { releaseSlot(nearSlot); nearSlot = -1; }
                }
            }
            Job job;
            job.id = s_nextId++;
            job.isRelease = release;
            if (release) {
                for (auto it = s_clicks.rbegin(); it != s_clicks.rend(); ++it) {
                    if (it->down && it->act <= act) { job.pressAt = it->act; break; }
                }
            }
            job.act = act;
            job.base = base;
            job.slot = slot;
            job.nearSlot = nearSlot;
            job.nearBase = nearSlot >= 0 ? nearBase : -1;
            job.where = pl->m_player1->getPosition();
            job.mode = mode;
            s_pressed.push_back({job.id, job.where, release});
            s_jobs.push_back(job);
        }

        // A press acted (or will act) on tick `act`.
        void onPress(PlayLayer* pl, int64_t act) {
            // Everything waiting now knows how long it had to survive.
            for (auto& j : s_jobs) {
                if (j.horizon < 0) j.horizon = std::max(j.act + 12, act - 4);
            }
            if (fromTable(pl, act, false)) return;
            startJob(pl, act, false);
        }

        // Letting go only matters in these modes; other releases still move
        // with their press, but get no window of their own.
        bool releaseCounts(PlayLayer* pl) {
            auto p = pl->m_player1;
            return p && (p->m_isDart || p->m_isShip || p->m_isRobot);
        }

        void onRelease(PlayLayer* pl, int64_t act) {
            for (auto it = s_jobs.rbegin(); it != s_jobs.rend(); ++it) {
                if (!it->isRelease && it->release < 0 && it->act <= act) { it->release = act; break; }
            }
            if (!releaseCounts(pl)) return;
            s_preTotal += s_pre == Pre::Loading ? 1 : 0;  // the loading screen counts it now
            if (fromTable(pl, act, true)) return;
            startJob(pl, act, true);
        }
    }

    bool active() { return cmm::on("framecounter"); }
}

namespace cmm::fw {
    bool loading() { return own::s_pre == own::Pre::Loading; }

    std::string debugStats() {
        std::string out;
        for (int m = 0; m < own::kModes; m++) {
            auto const& st = own::s_stats[m];
            if (!st.presses) continue;
            out += fmt::format("{}: {} presses, {} measured ({} ?, {} over 10), dropped: {} no history, {} no slot, {} death, {} reset; ",
                own::kModeNames[m], st.presses, st.measured, st.unknown, st.over10, st.noHistory, st.noSlot, st.death, st.reset);
        }
        return out.empty() ? "no presses" : out;
    }
    void resetDebugStats() { for (auto& st : own::s_stats) st = {}; }

    void noteBotFix(replay::Fix const& fix, unsigned progress) {
        if (!own::active()) return;
        // The counter's own tick for this one (its processCommands may or may
        // not have counted this tick yet).
        int64_t tick = progress == own::s_lastProgress ? own::s_tick : own::s_tick + 1;
        own::s_botFixes.push_back({tick, fix});
    }
}

using namespace cmm::fw;

// Counts ticks, keeps the player's recent states and notes orb use.
class $modify(FrameWindowGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        // First: the state is kept before anything else acts on this tick.
        (void)self.setHookPriority("GJBaseGameLayer::processCommands", Priority::First);
        // Last: see the clicks GD actually gets (after Click Before Frames).
        (void)self.setHookPriority("GJBaseGameLayer::handleButton", Priority::Last);
    }

    bool isMain() {
        auto pl = PlayLayer::get();
        return pl && static_cast<GJBaseGameLayer*>(pl) == this;
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (isMain() && own::active() && !cmm::sim::simulating()) {
            auto pl = static_cast<PlayLayer*>(static_cast<GJBaseGameLayer*>(this));
            unsigned progress = m_gameState.m_currentProgress;
            if (progress != own::s_lastProgress) {
                own::s_lastProgress = progress;
                own::s_tick++;
                int stride = own::stride();
                if (stride != own::s_ringStride) {  // accuracy changed: start the history over
                    own::s_ringStride = stride;
                    for (auto& t : own::s_ringTick) t = -1;
                    own::s_ringReady = false;
                }
                if (own::s_tick % stride == 0) {
                    int slot = static_cast<int>((own::s_tick / stride) % own::kRing);
                    if (auto p = cmm::sim::scratch(pl, own::kRingBase + slot); p && m_player1) {
                        cmm::sim::copyState(p, m_player1);
                        own::s_ringTick[slot] = own::s_tick;
                        own::s_ringReady = true;
                    }
                }
                int64_t keep = std::max<int64_t>(2000, own::range() + stride * 2 + 1000);
                while (!own::s_clicks.empty() && own::s_clicks.front().act < own::s_tick - keep) own::s_clicks.pop_front();
                while (!own::s_activations.empty() && own::s_activations.front().tick < own::s_tick - keep) own::s_activations.pop_front();
                while (!own::s_botFixes.empty() && own::s_botFixes.front().tick < own::s_tick - keep) own::s_botFixes.pop_front();
            }
        }
        own::s_inStep = true;
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        own::s_inStep = false;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        if (!isMain() || !own::active() || button != 1 || cmm::forPlayer2(!isPlayer1) || cmm::sim::simulating()) return;
        auto pl = static_cast<PlayLayer*>(static_cast<GJBaseGameLayer*>(this));
        if (pl->m_playerDied || pl->m_isPaused) return;
        // A press while already holding, or a release that ends nothing (a
        // second key, the bot letting go twice), changes nothing: not a click.
        if (down == own::s_held) return;
        own::s_held = down;
        int64_t act = own::s_inStep ? own::s_tick : own::s_tick + 2;
        cmm::replay::Fix fix;
        if (auto f = cmm::replay::injectingFix(); f && own::s_inStep) fix = *f;
        own::s_clicks.push_back({act, down, fix});
        if (down) own::s_stats[own::modeOf(m_player1)].presses++;
        if (down) own::onPress(pl, act);
        else own::onRelease(pl, act);
    }
};

class $modify(FrameWindowEnhanced, EnhancedGameObject) {
    bool hasBeenActivatedByPlayer(PlayerObject* player) {
        bool used = EnhancedGameObject::hasBeenActivatedByPlayer(player);
        if (!used && own::active() && !cmm::sim::simulating()) {
            auto pl = PlayLayer::get();
            if (pl && player == pl->m_player1) own::s_activations.push_back({this, own::s_tick});
        }
        return used;
    }
};

class $modify(FrameWindowLayer, PlayLayer) {
    struct Marker {
        Ref<CCNode> node;
        CCPoint where;     // position in the level
        int id = 0;        // the press it belongs to
    };

    struct Fields {
        // Both live on GD's HUD layer (m_uiLayer), which camera moves don't
        // shift: the list stays in its corner and markers are placed on screen
        // from their level position every frame.
        Ref<CCNode> markersRoot;
        Ref<CCNode> list;
        std::vector<Marker> markers;
        int counts[kBands] = {};  // NaN GD's bands 0-1 .. 8-10, then 10+
        std::vector<PrecisionClick> precisionClicks;  // measured clicks this attempt
        double precision = 0.0;                        // NaNDL L* (sigma/s)
        int unknown = 0;
        int presses = 0, releases = 0;  // measured this attempt
        bool haveLast = false;          // the last measured click, for the ms row
        double lastWindow = 0.0;
        bool lastRelease = false;
        std::string listKey;
        Ref<CCNode> loading;           // the loading screen
        bool preloadDone = false;      // the loading screen already ran on this visit
        Ref<CCLayerColor> loadingFill;
        Ref<CCLabelBMFont> loadingText;
    };

    // ---- pre-measuring ----

    bool macroForThisLevel() {
        if (!cmm::on("bot.play") || cmm::replay::botMacro().empty() || !m_level) return false;
        int id = m_level->m_levelID.value();
        if (cmm::replay::botLevelID() != 0 && id != 0) return cmm::replay::botLevelID() == id;
        auto const& name = cmm::replay::botLevel();
        return name.empty() || name == std::string(m_level->m_levelName);
    }

    void startGame() {
        PlayLayer::startGame();
        // Once per visit to the level: not again after Cancel, "Play anyway"
        // or a finished measurement restarts it.
        auto f = m_fields.self();
        if (f->preloadDone) return;
        if (own::active() && cmm::val<bool>("framecounter.preload", true) && macroForThisLevel()) {
            f->preloadDone = true;
            beginLoading();
        }
    }

    void beginLoading() {
        auto f = m_fields.self();
        own::s_pre = own::Pre::Loading;
        own::s_table.clear();
        own::s_preMeasured = 0;
        own::s_preTotal = 0;
        own::s_preDied = false;
        own::s_preAsking = false;
        own::s_preUnknown = 0;
        for (auto const& in : cmm::replay::botMacro()) {
            // Presses; releases are added as the run reaches them (only those
            // made as wave, ship or robot count).
            if (in.button == 1 && !cmm::forPlayer2(in.player2) && in.down) own::s_preTotal++;
        }
        own::s_preStepping = true;
        if (auto engine = FMODAudioEngine::sharedEngine(); engine && engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setPaused(true);
        }

        auto win = CCDirector::get()->getWinSize();
        auto root = CCLayerColor::create({8, 8, 12, 245});
        root->setID("cats-mod-menu-frame-window-loading");
        auto title = CCLabelBMFont::create("Measuring frame windows", "bigFont.fnt");
        title->setScale(0.7f);
        title->setPosition({win.width / 2, win.height / 2 + 40.f});
        root->addChild(title);
        float barW = win.width * 0.6f;
        auto back = CCLayerColor::create({50, 50, 60, 255}, barW, 12.f);
        back->setPosition({(win.width - barW) / 2, win.height / 2});
        root->addChild(back);
        auto fill = CCLayerColor::create({90, 190, 255, 255}, 1.f, 12.f);
        fill->setPosition(back->getPosition());
        root->addChild(fill);
        auto text = CCLabelBMFont::create("", "goldFont.fnt");
        text->setScale(0.6f);
        text->setPosition({win.width / 2, win.height / 2 - 25.f});
        root->addChild(text);
        auto cancel = CCMenuItemSpriteExtra::create(ButtonSprite::create("Cancel"), this, menu_selector(FrameWindowLayer::onCancelLoading));
        auto menu = CCMenu::create();
        menu->addChild(cancel);
        menu->setPosition({win.width / 2, win.height / 2 - 70.f});
        root->addChild(menu);
        CCDirector::get()->getRunningScene()->addChild(root, 10000);
        f->loading = root;
        f->loadingFill = fill;
        f->loadingText = text;
        // The mouse, for the buttons (GD hides it while playing).
        PlatformToolbox::showCursor();
        log::info("Frame windows: measuring {} clicks of the bot's macro before the level starts", own::s_preTotal);
        updateLoading();
    }

    void updateLoading() {
        auto f = m_fields.self();
        if (!f->loading) return;
        float done = own::s_preTotal > 0 ? static_cast<float>(own::s_preMeasured) / own::s_preTotal : 1.f;
        auto win = CCDirector::get()->getWinSize();
        f->loadingFill->setContentSize({std::max(1.f, win.width * 0.6f * std::clamp(done, 0.f, 1.f)), 12.f});
        if (own::s_preAsking) return;
        f->loadingText->setString(fmt::format("{} / {} clicks measured{}", own::s_preMeasured, own::s_preTotal,
            own::s_preUnknown ? fmt::format(" ({} unsure)", own::s_preUnknown) : std::string()).c_str());
    }

    // The loading screen's loop: run the level fast with the bot, measure
    // the presses, and when all are done start the level for real.
    void loadingFrame() {
        if (own::s_preAsking) return;
        auto start = std::chrono::steady_clock::now();
        auto elapsed = [&] { return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count(); };
        int64_t lastPress = 0;
        for (auto const& in : cmm::replay::botMacro()) {
            if (in.button == 1 && !cmm::forPlayer2(in.player2)) lastPress = in.tick;
        }
        // Keep slots free for the clicks still to come: wait for the measuring
        // to catch up instead of skipping clicks.
        while (own::s_preStepping && elapsed() < 10.f && own::freeSlots() >= 4) {
            if (m_playerDied || m_gameState.m_currentProgress > lastPress + 480 * cmm::cbf::progressPerTick()) {
                if (m_playerDied) {
                    own::s_preDied = true;
                    own::s_preDiedAt = this->getCurrentPercent();
                }
                own::s_preStepping = false;
                break;
            }
            static_cast<GJBaseGameLayer*>(this)->update(1.f / 240.f);
        }
        // A press with no next one: it has to survive half a second.
        for (auto& j : own::s_jobs) {
            if (j.horizon < 0 && (!own::s_preStepping || own::s_tick > j.act + 480)) j.horizon = j.act + 120;
        }
        own::work(this, std::max(2.f, 16.f - elapsed()));
        for (auto const& r : own::s_done) {
            own::s_table[r.rel * 2 + (r.release ? 1 : 0)] = {r.window, r.unknown};
            own::s_preMeasured++;
            if (r.unknown) own::s_preUnknown++;
        }
        own::s_done.clear();
        own::s_pressed.clear();
        updateLoading();
        if (own::s_preStepping || !own::s_jobs.empty()) return;
        if (own::s_preDied) askAfterDeath();
        else finishLoading(true);
    }

    // The macro died before its last click: it's probably for another level
    // (or an old recording). Say so instead of starting with clicks missing.
    void askAfterDeath() {
        auto f = m_fields.self();
        own::s_preAsking = true;
        log::warn("Frame windows: the macro died at {:.1f}% while measuring ({} of {} clicks measured)",
            own::s_preDiedAt, own::s_preMeasured, own::s_preTotal);
        if (!f->loading) return;
        f->loadingText->setString(fmt::format("The macro died at {:.0f}%: {} of {} clicks measured.\nIs it recorded on this level?",
            own::s_preDiedAt, own::s_preMeasured, own::s_preTotal).c_str());
        f->loadingText->setAlignment(kCCTextAlignmentCenter);
        auto win = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create();
        auto play = CCMenuItemSpriteExtra::create(ButtonSprite::create("Play anyway"), this, menu_selector(FrameWindowLayer::onPlayAnyway));
        menu->addChild(play);
        menu->setPosition({win.width / 2, win.height / 2 - 110.f});
        f->loading->addChild(menu);
    }

    void onPlayAnyway(CCObject*) {
        if (own::s_pre == own::Pre::Loading) finishLoading(true);
    }

    void finishLoading(bool keep) {
        auto f = m_fields.self();
        if (f->loading) f->loading->removeFromParent();
        // Back to how GD has it while playing ("Show cursor in-game" option).
        if (!GameManager::get()->getGameVariable("0024")) PlatformToolbox::hideCursor();
        f->loading = nullptr;
        own::s_pre = keep ? own::Pre::Ready : own::Pre::Off;
        own::s_preAsking = false;
        own::s_preDied = false;
        if (!keep) own::s_table.clear();
        log::info("Frame windows: {} ({} clicks measured)", keep ? "ready" : "cancelled", own::s_preMeasured);
        // Start the level for real (this also restarts the music).
        this->resetLevel();
    }

    void onCancelLoading(CCObject*) {
        if (own::s_pre == own::Pre::Loading) finishLoading(false);
    }

    // Finishing the level while fast-forwarding would end it for real.
    void levelComplete() {
        if (own::s_pre == own::Pre::Loading) {
            own::s_preStepping = false;
            return;
        }
        PlayLayer::levelComplete();
    }

    void pauseGame(bool unfocused) {
        if (own::s_pre == own::Pre::Loading) return;
        PlayLayer::pauseGame(unfocused);
    }

    CCNode* hud() { return m_uiLayer ? static_cast<CCNode*>(m_uiLayer) : static_cast<CCNode*>(this); }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        auto f = m_fields.self();
        auto markers = CCNode::create();
        markers->setID("cats-mod-menu-frame-window-markers");
        hud()->addChild(markers, 9998);
        f->markersRoot = markers;
        auto list = CCNode::create();
        list->setID("cats-mod-menu-frame-window-list");
        hud()->addChild(list, 9999);
        f->list = list;
        resetResults();
        own::clearAll();
    }

    // A ring with the window next to it ("?" in grey when unknown).
    // pending = placeholder shown at the press until it's measured.
    // Releases get a hollow ring.
    void spawnMarker(int id, CCPoint where, double window, bool unknown, bool pending = false, bool release = false) {
        auto f = m_fields.self();
        if (!f->markersRoot) return;
        removeMarker(id);
        auto c = pending ? ccColor3B{200, 200, 210} : unknown ? ccColor3B{150, 150, 160} : bandColor(window);
        float cr = c.r / 255.f, cg = c.g / 255.f, cb = c.b / 255.f;
        // CCDrawNode blends with pre-multiplied alpha: scale colours by alpha.
        auto tint = [&](float a) { return ccColor4F{cr * a, cg * a, cb * a, a}; };

        auto node = CCNode::create();
        auto ring = CCDrawNode::create();
        auto circle = [&](float radius, ccColor4F fill, float width, ccColor4F line) {
            constexpr int n = 48;
            CCPoint v[n];
            for (int i = 0; i < n; i++) {
                float a = 2.f * static_cast<float>(M_PI) * i / n;
                v[i] = CCPoint(radius * std::cos(a), radius * std::sin(a));
            }
            ring->drawPolygon(v, n, fill, width, line);
        };
        circle(17.f, {0, 0, 0, 0}, 3.f, tint(0.22f));          // soft glow
        // Releases are circles too, hollow inside so they read apart from presses.
        circle(13.f, release ? ccColor4F{0, 0, 0, 0} : tint(0.28f), 4.f, {0.f, 0.f, 0.f, 0.85f}); // dark outline, tinted inside
        circle(13.f, {0, 0, 0, 0}, 2.f, tint(1.f));             // the coloured ring
        node->addChild(ring);

        auto text = pending ? std::string("...") : unknown ? std::string("?") : fmt::format("{:.2f}", window);
        auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        label->setColor(c);
        label->setAnchorPoint({1.f, 0.5f});
        label->setPosition({-19.f, 0.f});
        node->addChild(label);
        if (pending) {
            label->setScale(0.4f);
            label->setOpacity(170);
            ring->setScale(0.85f);
        } else {
            label->setScale(0.75f);
            label->runAction(CCEaseBackOut::create(CCScaleTo::create(0.25f, 0.5f)));
            ring->setScale(1.3f);
            ring->runAction(CCEaseBackOut::create(CCScaleTo::create(0.25f, 1.f)));
        }
        f->markersRoot->addChild(node);
        f->markers.push_back({node, where, id});
        placeMarkers();
    }

    void removeMarker(int id) {
        if (id == 0) return;
        auto f = m_fields.self();
        for (auto it = f->markers.begin(); it != f->markers.end();) {
            if (it->id == id) {
                if (it->node) it->node->removeFromParent();
                it = f->markers.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Level position -> HUD position, scaled with the camera zoom.
    void placeMarkers() {
        auto f = m_fields.self();
        if (!m_objectLayer || !f->markersRoot) return;
        auto win = CCDirector::get()->getWinSize();
        float zoom = std::abs(m_objectLayer->getScaleY());
        for (auto it = f->markers.begin(); it != f->markers.end();) {
            auto screen = m_objectLayer->convertToWorldSpace(it->where);
            it->node->setPosition(hud()->convertToNodeSpace(screen));
            it->node->setScale(zoom);
            if (screen.x < -400.f || screen.x > win.width + 2000.f) {
                it->node->removeFromParent();
                it = f->markers.erase(it);
            } else {
                ++it;
            }
        }
    }

    void clearMarkers() {
        auto f = m_fields.self();
        for (auto& m : f->markers) {
            if (m.node) m.node->removeFromParent();
        }
        f->markers.clear();
    }

    // Tally of the clicks measured this attempt, one row per colour band
    // (NaN GD's order, biggest windows first), in the top-left corner.
    void updateList() {
        auto f = m_fields.self();
        if (!f->list) return;
        bool show = cmm::on("framecounter");
        f->list->setVisible(show);
        if (!show) return;

        // A replay Click Before Frames can't use here: say why.
        std::string note;
        if (!cmm::cbf::replayName().empty() && cmm::on("cbf") && !cmm::cbf::replayUsable(this)) {
            note = fmt::format("CBF replay \"{}\" is for {}", cmm::cbf::replayName(),
                cmm::cbf::replayLevel().empty() ? "another level" : cmm::cbf::replayLevel());
        }
        int total = f->unknown;
        for (int c : f->counts) total += c;
        auto key = fmt::format("{}|{}|{}|{}|{}|{}|{}", fmt::join(f->counts, ","), f->unknown, own::s_jobs.size(), note,
            f->presses, f->releases, f->haveLast ? fmt::format("{:.2f}{}|{:.3f}", f->lastWindow, f->lastRelease, f->precision) : "");
        if (key == f->listKey) return;
        f->listKey = key;

        f->list->removeAllChildren();
        auto win = CCDirector::get()->getWinSize();
        float y = hud()->convertToNodeSpace({0.f, win.height}).y - 5.f;
        float left = hud()->convertToNodeSpace({0.f, 0.f}).x + 5.f;
        auto addRow = [&](std::string const& name, std::string const& value, ccColor3B color) {
            auto text = CCLabelBMFont::create(name.c_str(), "bigFont.fnt");
            text->setScale(0.42f);
            text->setAnchorPoint({0.f, 1.f});
            text->setPosition({left, y});
            text->setColor(color);
            f->list->addChild(text);
            auto count = CCLabelBMFont::create(value.c_str(), "bigFont.fnt");
            count->setScale(0.42f);
            count->setAnchorPoint({0.f, 1.f});
            count->setPosition({left + 62.f, y});
            f->list->addChild(count);
            y -= text->getScaledContentSize().height + 1.f;
        };
        auto addSmall = [&](std::string const& line) {
            auto label = CCLabelBMFont::create(line.c_str(), "bigFont.fnt");
            label->setScale(0.3f);
            label->setAnchorPoint({0.f, 1.f});
            label->setPosition({left, y - 2.f});
            label->setColor({200, 200, 200});
            f->list->addChild(label);
            y -= label->getScaledContentSize().height + 1.f;
        };
        bool hide10 = cmm::val<bool>("framecounter.hide10", true);
        if (!hide10) addRow(kBandNames[8], std::to_string(f->counts[8]), kBandColors[8]);
        for (int b = 7; b >= 0; b--) addRow(kBandNames[b], std::to_string(f->counts[b]), kBandColors[b]);
        if (hide10) addRow(kBandNames[8], std::to_string(f->counts[8]), {150, 150, 160});
        if (f->unknown) addRow("?", std::to_string(f->unknown), {150, 150, 160});
        // The last click's window, as NaN GD shows it: in milliseconds (one
        // frame = 1000/240 ms) and as a rate (1000 / ms).
        if (f->haveLast) {
            double ms = f->lastWindow * 1000.0 / 240.0;
            auto c = bandColor(f->lastWindow);
            addRow(fmt::format("{:.2f}ms", ms), "", c);
            addRow(fmt::format("{:.0f}Hz", ms > 0.0 ? 1000.0 / ms : 0.0), f->lastRelease ? "(release)" : "", c);
        }
        // Precision (NaNDL L*): needed to beat this much of the level within
        // 24 hours of tries; goes up with every click.
        if (cmm::val<bool>("framecounter.precision", true) && !f->precisionClicks.empty())
            addRow("L*", fmt::format("{:.2f}", f->precision), {255, 200, 120});
        addSmall(fmt::format("{} presses, {} releases measured{}", f->presses, f->releases,
            own::s_jobs.empty() ? "" : fmt::format(" ({} waiting)", own::s_jobs.size())));
        (void)total;
        if (!note.empty()) addSmall(note);
    }

    void resetResults() {
        s_replaySize = cmm::cbf::replayInputs().size();
        s_results.assign(s_replaySize, {});
    }

    void resetCounter() {
        auto f = m_fields.self();
        clearMarkers();
        for (auto& c : f->counts) c = 0;
        f->unknown = 0;
        f->presses = f->releases = 0;
        f->haveLast = false;
        f->precisionClicks.clear();
        f->precision = 0.0;
        f->listKey.clear();
        own::s_done.clear();
    }

    void resetLevel() {
        // The macro died while fast-forwarding: GD wants to respawn on its own,
        // but the loading screen is still up (measuring the rest, or asking).
        if (own::s_pre == own::Pre::Loading && own::s_preDied) return;
        PlayLayer::resetLevel();
        own::s_attemptStart = own::s_tick;
        // Clicks waiting to be measured belong to the attempt that ended.
        for (auto& j : own::s_jobs) {
            if (!j.isRelease) own::s_stats[j.mode].reset++;
            own::releaseSlot(j.slot);
            removeMarker(j.id);
        }
        own::s_pressed.clear();
        own::s_jobs.clear();
        own::s_clicks.clear();
        own::s_activations.clear();
        own::s_held = false;
        // A new attempt starts a new tally; a practice respawn keeps it.
        if (!m_isPracticeMode) resetCounter();

        // Replay windows for Click Before Frames: forget the ones ahead.
        unsigned now = m_gameState.m_currentProgress;
        auto const& inputs = cmm::cbf::replayInputs();
        if (inputs.size() != s_results.size()) { resetResults(); return; }
        for (size_t i = 0; i < inputs.size(); i++) {
            if (inputs[i].tick >= now) s_results[i] = {};
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool wasDead = m_playerDied;
        PlayLayer::destroyPlayer(player, object);
        // Presses still waiting can't be measured fairly past a death.
        if (!wasDead && m_playerDied && !cmm::sim::simulating()) {
            for (auto& j : own::s_jobs) {
                if (j.horizon < 0) {
                    if (!j.isRelease) own::s_stats[j.mode].death++;
                    own::releaseSlot(j.slot);
                    own::s_dropped.push_back(j.id);
                }
            }
            own::s_jobs.erase(std::remove_if(own::s_jobs.begin(), own::s_jobs.end(), [](auto const& j) { return j.horizon < 0; }), own::s_jobs.end());
        }
    }

    void onQuit() {
        auto f = m_fields.self();
        if (f->loading) f->loading->removeFromParent();
        f->loading = nullptr;
        own::s_pre = own::Pre::Off;
        own::s_table.clear();
        clearMarkers();
        own::clearAll();
        PlayLayer::onQuit();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto f = m_fields.self();

        // Replay windows for Click Before Frames (only with a replay for this level).
        auto const& inputs = cmm::cbf::replayInputs();
        if (inputs.size() != s_replaySize) resetResults();
        if (analyserOn() && !m_playerDied && m_player1 && !inputs.empty() && cmm::cbf::replayUsable(this)) {
            unsigned now = m_gameState.m_currentProgress;
            unsigned ppt = std::max(1u, cmm::cbf::progressPerTick());
            int lead = std::max(searchRange(), cmm::cbf::windowTicks()) + 3;
            for (size_t i = 0; i < inputs.size(); i++) {
                auto const& in = inputs[i];
                if (in.button != 1 || cmm::forPlayer2(in.player2) || in.tick <= now) continue;
                if ((in.tick - now) / ppt > static_cast<unsigned>(lead)) break;
                if (!s_results[i].analysed) analyse(this, inputs, i, now, ppt);
            }
        }

        // While pre-measuring, the loading screen's loop does the work.
        if (own::s_pre == own::Pre::Loading) {
            for (auto const& r : own::s_done) {
                own::s_table[r.rel * 2 + (r.release ? 1 : 0)] = {r.window, r.unknown};
                own::s_preMeasured++;
                if (r.unknown) own::s_preUnknown++;
            }
            own::s_done.clear();
            own::s_pressed.clear();
            updateLoading();
            return;
        }

        // The counter: measure waiting presses a bit every frame.
        if (own::active() && !m_playerDied) {
            // No next press for 2 seconds: it has to survive half a second.
            for (auto& j : own::s_jobs) {
                if (j.horizon < 0 && own::s_tick > j.act + 480) j.horizon = j.act + 120;
            }
            // More time per frame while clicks pile up (spam sections).
            float budget = std::clamp(cmm::val<float>("framecounter.budget", 1.5f), 0.2f, 8.f);
            size_t waiting = own::s_jobs.size();
            if (waiting > 4) budget = std::min(budget * (1.f + static_cast<float>(waiting - 4) / 4.f), 8.f);
            own::work(this, budget);
        }
        // A placeholder at every new press, replaced once it's measured.
        for (auto const& p : own::s_pressed) spawnMarker(p.id, p.where, 0.0, false, true, p.release);
        own::s_pressed.clear();
        for (int id : own::s_dropped) removeMarker(id);
        own::s_dropped.clear();
        bool hideUnsure = cmm::val<bool>("framecounter.hideunsure", true);
        for (auto const& r : own::s_done) {
            // Hide unsure: a click that couldn't be measured gets no marker.
            if (r.unknown && hideUnsure) {
                removeMarker(r.id);
                continue;
            }
            if (r.unknown) f->unknown++;
            else f->counts[bandOf(r.window)]++;
            (r.release ? f->releases : f->presses)++;
            if (!r.unknown) {
                double respawn = std::clamp(cmm::val<float>("framecounter.respawn", 0.f), 0.f, 10.f);
                auto& pc = f->precisionClicks;
                pc.push_back({r.window, static_cast<double>(r.rel) / 240.0 + respawn});
                std::stable_sort(pc.begin(), pc.end(), [](auto const& a, auto const& b) { return a.time < b.time; });
                f->precision = precisionL(pc);
                f->haveLast = true;
                f->lastWindow = r.window;
                f->lastRelease = r.release;
            }
            if (r.unknown || shown(r.window)) {
                spawnMarker(r.id, r.where, r.window, r.unknown, false, r.release);
                if (!r.unknown) ding(r.window);
            } else {
                removeMarker(r.id);
            }
        }
        own::s_done.clear();

        updateList();
        bool show = cmm::on("framecounter");
        if (f->markersRoot) f->markersRoot->setVisible(show);
        if (show) placeMarkers();
    }
};

// Drives the loading screen: once per frame, after the game's own update.
class $modify(FrameWindowScheduler, CCScheduler) {
    void update(float dt) {
        CCScheduler::update(dt);
        if (own::s_pre != own::Pre::Loading) return;
        if (auto pl = PlayLayer::get()) static_cast<FrameWindowLayer*>(pl)->loadingFrame();
    }
};
