#include <core/Compat.hpp>
#include "ClickBeforeFrames.hpp"
#include "Replay.hpp"
#include "FrameWindow.hpp"
#include <core/Hacks.hpp>
#include <core/Players.hpp>
#include <core/Log.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>

using namespace geode::prelude;

// How it works
// ------------
// You pick a macro recorded with the Replay Bot. Every time you press or let
// go of jump, before GD sees it:
//
// 1. We look for the replay's next unused click of the same kind (press or
//    release, same player) at or after your tick, no more than the stage's
//    limit ahead.
// 2. If there is one, your click is held back and given to GD so it acts on
//    exactly the tick the replay's click did ("SAVED +k TICKS").
// 3. If not, your click goes through untouched.
//
// Timing, measured in game: a click made between frames acts 2 ticks after
// the tick it came in on; a click we give to GD at the start of a tick acts
// on that tick. The replay stores the tick its clicks came in on, so both
// line up when we give the click back at "replay tick + 2".
//
// Clicks you make while one is held back keep their spacing (a hold stays a
// hold) unless they snap to a replay click of their own.

namespace cmm::cbf {
    namespace {
        struct Stage {
            int all;
            int wave;
        };
        constexpr Stage kStages[] = {{0, 0}, {2, 2}, {4, 4}, {8, 8}, {16, 8}, {32, 32}, {64, 64}, {96, 96}, {128, 128}};

        struct Queued {
            bool down;
            bool player1;
            int64_t applyAt;
        };

        std::deque<Queued> s_queue;
        bool s_injecting = false;
        int64_t s_tick = 0;
        unsigned s_lastProgress = ~0u;
        unsigned s_progressPerTick = 2;  // GD's progress counter moves 2 per tick
        int s_session = 0;
        int s_attempt = 0;
        int s_chainDelay = 0;
        Decision s_last;

        std::string s_replayName;
        std::string s_replayLevel;
        int s_replayLevelID = 0;
        std::vector<replay::Input> s_replay;
        std::vector<bool> s_used;

        bool isMain(GJBaseGameLayer* layer) {
            auto pl = PlayLayer::get();
            return pl && static_cast<GJBaseGameLayer*>(pl) == layer;
        }

        void writeLog(PlayLayer* pl, std::string const& msg) {
            if (!val<bool>("cbf.logfile", true)) return;
            auto level = pl->m_level;
            std::string name = level ? std::string(level->m_levelName) : "Unknown";
            int id = level ? level->m_levelID.value() : 0;
            auto line = fmt::format("[{}] {} ({}) att {} @ {:.2f}% {} [Replay: {}]",
                log::timestamp(true), name, id, pl->m_attempts, pl->getCurrentPercent(), msg, s_replayName);

            std::error_code ec;
            auto path = std::filesystem::path(logPath().native());
            std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream file(path, std::ios::app);
            if (!file) {
                log::error("Could not write to CBF-LOGS.txt");
                return;
            }
            file << line << "\n";
        }

        // The replay click this input should land on, or -1.
        int findSnap(unsigned progress, bool down, bool player2, int limit) {
            if (s_replay.empty() || limit <= 0) return -1;
            unsigned maxAhead = static_cast<unsigned>(limit) * s_progressPerTick;
            auto it = std::lower_bound(s_replay.begin(), s_replay.end(), progress,
                [](replay::Input const& in, unsigned p) { return in.tick < p; });
            for (; it != s_replay.end() && it->tick <= progress + maxAhead; ++it) {
                size_t i = static_cast<size_t>(it - s_replay.begin());
                if (s_used[i] || it->button != 1 || it->down != down || cmm::forPlayer2(it->player2) != cmm::forPlayer2(player2)) continue;
                return static_cast<int>(i);
            }
            // With the Frame Window Analyser, a click is in reach when the
            // earliest tick of its window is, even if the replay's tick isn't.
            for (; it != s_replay.end() && it->tick <= progress + maxAhead + 64u * s_progressPerTick; ++it) {
                size_t i = static_cast<size_t>(it - s_replay.begin());
                if (s_used[i] || it->button != 1 || it->down != down || cmm::forPlayer2(it->player2) != cmm::forPlayer2(player2)) continue;
                unsigned earliest = 0;
                if (cmm::fw::earliestFor(i, earliest) && earliest <= progress + maxAhead) return static_cast<int>(i);
                break;  // the next click of this kind is out of reach
            }
            return -1;
        }

        void report(PlayLayer* pl, Decision const& d, int limit) {
            auto msg = fmt::format("*** SAVED +{} TICKS ({:.2f} frames @ 60fps) {}! (Frame0: {} -> Delayed: {}) [Limit: {}]",
                d.delay, d.delay * 60.0 / 240.0, d.down ? "PRESS" : "RELEASE", d.yourTick, d.replayTick, limit);
            log::cbf(msg);
            writeLog(pl, msg);
        }

        void inject(PlayLayer* pl, Queued const& q) {
            s_injecting = true;
            pl->handleButton(q.down, 1, q.player1);
            s_injecting = false;
        }

        void flush(PlayLayer* pl) {
            while (!s_queue.empty() && s_queue.front().applyAt <= s_tick) {
                auto q = s_queue.front();
                s_queue.pop_front();
                inject(pl, q);
            }
        }

        // Pause/restart/death: releases still go through so nothing is stuck,
        // held-back presses are dropped.
        void dropQueue(PlayLayer* pl) {
            auto queue = std::move(s_queue);
            s_queue.clear();
            for (auto const& q : queue) {
                if (!q.down) inject(pl, q);
            }
        }

        void tick(PlayLayer* pl) {
            unsigned progress = pl->m_gameState.m_currentProgress;
            if (progress == s_lastProgress) return;
            if (s_lastProgress != ~0u && progress > s_lastProgress && progress - s_lastProgress <= 4) {
                s_progressPerTick = progress - s_lastProgress;
            }
            s_lastProgress = progress;
            s_tick++;
            if (s_queue.empty()) s_chainDelay = 0;
            else flush(pl);
        }
    }

    int stageCount() { return static_cast<int>(std::size(kStages)); }
    int stage() { return std::clamp(val<int>("cbf.stage", 3), 0, stageCount() - 1); }
    void setStage(int value) {
        save<int>("cbf.stage", std::clamp(value, 0, stageCount() - 1));
        log::cbf(fmt::format("Cycle -> STAGE {}", stageText(stage())));
    }
    void cycleStage() { setStage((stage() + 1) % stageCount()); }

    std::string stageText(int index) {
        auto const& s = kStages[std::clamp(index, 0, stageCount() - 1)];
        if (s.all == 0) return fmt::format("{}: OFF (100% Raw Input)", index);
        if (s.all == s.wave) {
            return fmt::format("{}: {} Ticks All | {} Ticks Wave ({:.1f}f @ 60fps)", index, s.all, s.wave, s.all / 4.0);
        }
        return fmt::format("{}: {} Ticks All | {} Ticks Wave ({:.1f}f / {:.1f}f @ 60fps)", index, s.all, s.wave, s.all / 4.0, s.wave / 4.0);
    }

    int limitFor(bool wave) {
        auto const& s = kStages[stage()];
        return wave ? s.wave : s.all;
    }
    int windowTicks() { return kStages[stage()].all; }

    std::string const& replayName() { return s_replayName; }
    std::string const& replayLevel() { return s_replayLevel; }

    bool replayUsable(PlayLayer* pl) {
        if (s_replay.empty() || !pl || !pl->m_level) return false;
        int id = pl->m_level->m_levelID.value();
        if (s_replayLevelID != 0 && id != 0) return s_replayLevelID == id;
        if (!s_replayLevel.empty()) return s_replayLevel == std::string(pl->m_level->m_levelName);
        return true;  // recorded before levels were saved: can't check
    }
    int replayClicks() { return static_cast<int>(s_replay.size()); }
    std::vector<replay::Input> const& replayInputs() { return s_replay; }
    unsigned progressPerTick() { return s_progressPerTick; }
    bool injecting() { return s_injecting; }

    bool setReplay(std::string const& name) {
        std::vector<replay::Input> inputs;
        std::string level;
        int levelID = 0;
        if (!name.empty() && !replay::readMacro(name, inputs, level, levelID)) {
            log::error("CBF: could not read macro {}", name);
            return false;
        }
        s_replay = std::move(inputs);
        s_replayLevel = level;
        s_replayLevelID = levelID;
        s_used.assign(s_replay.size(), false);
        s_replayName = name;
        save<std::string>("cbf.replay", name);
        if (!name.empty()) log::info("CBF now follows macro {} ({} clicks)", name, s_replay.size());
        return true;
    }

    void reloadReplay() {
        auto name = val<std::string>("cbf.replay", "");
        if (!name.empty()) setReplay(name);
    }

    int64_t currentTick() { return s_tick; }
    int savedThisSession() { return s_session; }
    int savedThisAttempt() { return s_attempt; }
    decltype(std::declval<geode::Mod&>().getSaveDir() / "x") logPath() { return Mod::get()->getSaveDir() / "CBF-LOGS.txt"; }
    Decision const& lastDecision() { return s_last; }
}

using namespace cmm::cbf;

class $modify(CBFGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        // See your input before the replay bot / trail do, so they record
        // the input GD actually gets.
        (void)self.setHookPriority("GJBaseGameLayer::handleButton", Priority::First);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (s_injecting || button != 1 || !cmm::on("cbf") || stage() == 0 || !isMain(this)) {
            return GJBaseGameLayer::handleButton(down, button, isPlayer1);
        }
        auto pl = static_cast<PlayLayer*>(static_cast<GJBaseGameLayer*>(this));
        auto real = cmm::forPlayer2(!isPlayer1) ? m_player2 : m_player1;
        int limit = limitFor(real && real->m_isDart);
        unsigned progress = m_gameState.m_currentProgress;

        s_last = {};
        s_last.tick = s_tick;
        s_last.down = down;
        s_last.yourTick = progress / s_progressPerTick;

        int snap = (pl->m_playerDied || pl->m_isPaused || !replayUsable(pl)) ? -1 : findSnap(progress, down, !isPlayer1, limit);
        if (snap < 0) {
            // Nothing to snap to. Pass through, or keep spacing behind a held-back click.
            if (s_queue.empty()) return GJBaseGameLayer::handleButton(down, button, isPlayer1);
            s_queue.push_back({down, isPlayer1, std::max(s_queue.back().applyAt + 1, s_tick + 2 + s_chainDelay)});
            return;
        }

        auto const& target = s_replay[static_cast<size_t>(snap)];
        s_used[static_cast<size_t>(snap)] = true;
        // With the Frame Window Analyser on, aim for the earliest tick of that
        // click's window instead of the replay's exact tick.
        unsigned aim = target.tick;
        unsigned earliest = 0;
        bool haveWindow = cmm::fw::earliestFor(static_cast<size_t>(snap), earliest);
        if (haveWindow) aim = std::min(aim, earliest);
        log::debug("CBF decide: your progress {} -> replay click {} at {} (window earliest {}, known {}) -> aim {}",
            progress, snap, target.tick, earliest, haveWindow, aim);
        int delay = aim > progress ? static_cast<int>((aim - progress) / s_progressPerTick) : 0;
        s_last.delay = delay;
        s_last.replayTick = aim / s_progressPerTick;

        // A click aimed at a window edge must act on exactly the tested tick:
        // clicks passed straight to GD can land a tick late with the frame
        // timing, so those go through the queue too.
        if (delay == 0 && s_queue.empty() && !haveWindow) return GJBaseGameLayer::handleButton(down, button, isPlayer1);

        int64_t applyAt = s_tick + 2 + delay;
        if (!s_queue.empty()) applyAt = std::max(applyAt, s_queue.back().applyAt + 1);
        s_queue.push_back({down, isPlayer1, applyAt});
        s_chainDelay = static_cast<int>(applyAt - s_tick - 2);
        if (delay == 0) return;
        s_session++;
        s_attempt++;
        report(pl, s_last, limit);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (isMain(this)) tick(static_cast<PlayLayer*>(static_cast<GJBaseGameLayer*>(this)));
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
};

class $modify(CBFPlayLayer, PlayLayer) {
    bool init(CMM_PL_INIT_PARAMS) {
        if (!PlayLayer::init(CMM_PL_INIT_ARGS)) return false;
        // Pick up changes to the macro file (re-recorded, etc.).
        cmm::cbf::reloadReplay();
        return true;
    }

    void pauseGame(bool unfocused) {
        dropQueue(this);
        PlayLayer::pauseGame(unfocused);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        if (m_playerDied) dropQueue(this);
    }

    void resetLevel() {
        dropQueue(this);
        PlayLayer::resetLevel();
        s_lastProgress = ~0u;
        s_attempt = 0;
        s_chainDelay = 0;
        // Every replay click can be used again (a respawn rewinds the ticks).
        std::fill(s_used.begin(), s_used.end(), false);
    }

    void onQuit() {
        s_queue.clear();
        PlayLayer::onQuit();
    }
};
