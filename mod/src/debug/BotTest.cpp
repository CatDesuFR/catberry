// Developer test of the Replay Bot. Only runs if "selftest-bot.txt" exists in
// the mod's save folder (deleted when it starts). On main level 20 with
// noclip it:
//   1. records scripted clicks (made between frames, like yours)
//   2. plays the macro back with position fixes
//   3. plays it back without fixes (as before)
//   4. plays it back with fixes at 144 FPS
// and writes "BOTTEST" lines to the Geode log: how far the player drifted
// from the recording, and whether each click acted on the same tick.
#include <core/Hacks.hpp>
#include <features/Replay.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>

using namespace geode::prelude;

namespace cmm::global { void applyFps(); }

namespace {
    bool s_active = false;
    bool s_keys = false;  // press a key through GD's keyboard handler (like real input)
    enumKeyCodes s_key = KEY_Space;  // Space, or Up (GD's player 2 key)
    int s_run = 0;  // 0 record, 1 play+fixes, 2 play without fixes, 3 play+fixes at 144 FPS, 4 like the old bot
    int s_frames = 0;
    int s_nextClick = 0, s_release = -1;
    unsigned s_seed = 12345;
    GJGameLevel* s_level = nullptr;
    std::vector<std::pair<std::string, bool>> s_saved;
    bool s_oldSooner = true;
    float s_oldFps = 240.f;
    int s_oldMaxDeaths = 0;
    float s_oldMinAcc = 0.f;

    std::map<unsigned, CCPoint> s_path[5];      // progress -> player 1 position
    std::vector<unsigned> s_pushes[5];          // progress when player 1 got a press / release
    std::vector<unsigned> s_arrivals;           // progress when each scripted click came in
    constexpr int kFrames = 240 * 25;          // ~25 s per run
    char const* const kRuns[] = {"record", "play with fixes", "play without fixes", "play with fixes at 144 FPS",
                                 "like the old bot (clicks a tick late, no fixes)"};

    std::filesystem::path saveDir() { return std::filesystem::path(Mod::get()->getSaveDir().native()); }

    void setHack(std::string_view id, bool on) {
        if (auto h = cmm::find(id); h && h->enabled != on) cmm::setEnabled(*h, on);
    }

    unsigned rnd(unsigned lo, unsigned hi) {
        s_seed = s_seed * 1103515245u + 12345u;
        return lo + (s_seed >> 8) % (hi - lo + 1);
    }

    void compare(int run) {
        double worst = 0, sum = 0;
        int n = 0;
        unsigned worstAt = 0;
        for (auto const& [tick, a] : s_path[0]) {
            auto it = s_path[run].find(tick);
            if (it == s_path[run].end()) continue;
            double d = std::hypot(a.x - it->second.x, a.y - it->second.y);
            sum += d;
            n++;
            if (d > worst) { worst = d; worstAt = tick; }
        }
        size_t same = 0, total = std::min(s_pushes[0].size(), s_pushes[run].size());
        for (size_t i = 0; i < total; i++) same += s_pushes[0][i] == s_pushes[run][i];
        log::info("BOTTEST {}: drift max {:.4f} units (at progress {}), average {:.5f} over {} ticks; clicks on the same tick {}/{} (recorded {}, played {})",
            kRuns[run], worst, worstAt, n ? sum / n : 0.0, n, same, total, s_pushes[0].size(), s_pushes[run].size());
    }

    void startRun(PlayLayer* pl) {
        s_frames = 0;
        s_seed = 12345;
        s_nextClick = 60;
        s_release = -1;
        if (s_run == 0) {
            cmm::replay::clearBot();
            setHack("bot.play", false);
            setHack("bot.record", true);
        } else {
            setHack("bot.record", false);
            cmm::replay::loadIntoBot("cmm-bottest");
            if (s_run == 2 || s_run == 4) {
                auto& m = const_cast<std::vector<cmm::replay::Input>&>(cmm::replay::botMacro());
                for (auto& in : m) {
                    in.fix.ok = false;
                    if (s_run == 4) in.tick += 2;  // the old bot played every click one tick late
                }
            }
            if (s_run == 4) {
                cmm::val<float>("fpsbypass.value", 240.f) = s_oldFps;
                cmm::global::applyFps();
            }
            setHack("bot.play", true);
            if (s_run == 3) {
                cmm::val<float>("fpsbypass.value", 240.f) = 144.f;
                setHack("fpsbypass", true);
                cmm::global::applyFps();
            }
        }
        pl->resetLevel();
    }

    void finish() {
        for (auto& [id, on] : s_saved) setHack(id, on);
        s_saved.clear();
        cmm::val<bool>("bot.sooner", true) = s_oldSooner;
        cmm::val<float>("fpsbypass.value", 240.f) = s_oldFps;
        cmm::val<int>("noclip.maxdeaths", 0) = s_oldMaxDeaths;
        cmm::val<float>("noclip.minacc", 0.f) = s_oldMinAcc;
        cmm::global::applyFps();
        cmm::replay::clearBot();
        std::error_code ec;
        // Kept as cmm-bottest-keep for the counter's loading-screen test.
        std::filesystem::remove(saveDir() / "macros" / "cmm-bottest-keep.txt", ec);
        std::filesystem::rename(saveDir() / "macros" / "cmm-bottest.txt", saveDir() / "macros" / "cmm-bottest-keep.txt", ec);
        s_active = false;
        log::info("BOTTEST DONE");
    }
}

// Every click GD hands to the level, before any other feature sees it.
class $modify(BotTestButtons, GJBaseGameLayer) {
    static void onModify(auto& self) { (void)self.setHookPriority("GJBaseGameLayer::handleButton", Priority::VeryEarly); }
    void handleButton(bool down, int button, bool isPlayer1) {
        static int s_seen = 0;
        if (s_active && s_run == 0 && ++s_seen <= 12)
            log::info("BOTTEST handleButton down={} button={} isPlayer1={} progress={}", down, button, isPlayer1, m_gameState.m_currentProgress);
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(BotTestPlayer, PlayerObject) {
    bool pushButton(PlayerButton b) {
        bool r = PlayerObject::pushButton(b);
        auto pl = PlayLayer::get();
        if (s_active && pl && this == pl->m_player1 && b == PlayerButton::Jump) s_pushes[s_run].push_back(pl->m_gameState.m_currentProgress * 2);
        return r;
    }
    bool releaseButton(PlayerButton b) {
        bool r = PlayerObject::releaseButton(b);
        auto pl = PlayLayer::get();
        if (s_active && pl && this == pl->m_player1 && b == PlayerButton::Jump) s_pushes[s_run].push_back(pl->m_gameState.m_currentProgress * 2 + 1);
        return r;
    }
};

class $modify(BotTestLayer, PlayLayer) {
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool was = m_playerDied;
        PlayLayer::destroyPlayer(player, object);
        if (s_active && !was && m_playerDied)
            log::info("BOTTEST killed by object {} (anti-cheat spike: {}, player 2: {}, noclip on: {})", object ? object->m_objectID : -1,
                object == m_anticheatSpike, player == m_player2, cmm::on("noclip"));
    }
    void resetLevel() {
        if (s_active && s_run == 0) log::info("BOTTEST level restarted while recording at progress {} (x {:.0f}, died {})", m_gameState.m_currentProgress, m_player1->getPositionX(), m_playerDied);
        PlayLayer::resetLevel();
    }
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!s_active || PlayLayer::get() != this) return;
        if (m_level != s_level) { finish(); log::info("BOTTEST stopped (left the level)"); return; }
        if (m_gameState.m_currentProgress == 0) return;  // level not moving yet
        s_path[s_run][m_gameState.m_currentProgress] = m_player1->m_position;
        s_frames++;
        // Scripted clicks between frames, only while recording.
        if (s_run == 0) {
            if (s_frames == s_nextClick) {
                s_arrivals.push_back(m_gameState.m_currentProgress * 2);
                if (s_keys) CCKeyboardDispatcher::get()->dispatchKeyboardMSG(s_key, true, false, geode::utils::getInputTimestamp());
                else this->handleButton(true, 1, true);
                s_release = s_frames + static_cast<int>(rnd(3, 45));
                s_nextClick = s_release + static_cast<int>(rnd(8, 90));
            }
            if (s_frames == s_release) {
                s_arrivals.push_back(m_gameState.m_currentProgress * 2 + 1);
                if (s_keys) CCKeyboardDispatcher::get()->dispatchKeyboardMSG(s_key, false, false, geode::utils::getInputTimestamp());
                else this->handleButton(false, 1, true);
            }
        }
        int limit = s_run == 3 ? kFrames * 144 / 240 : kFrames;
        if (s_frames < limit) return;
        if (s_run == 0) {
            if (s_release > s_frames - 1 && s_release >= 0) this->handleButton(false, 1, true);
            setHack("bot.record", false);
            cmm::replay::saveCurrent("cmm-bottest");
            log::info("BOTTEST recorded {} clicks", cmm::replay::botInputs());
            {
                auto const& m = cmm::replay::botMacro();
                int p2 = 0, fixes = 0;
                for (auto const& in : m) { p2 += in.player2; fixes += in.fix.ok; }
                log::info("BOTTEST {} of them marked player 2, {} with a position fix", p2, fixes);
            }
            // How many ticks after coming in each click really acted (the old
            // bot assumed always one tick).
            std::map<int, int> delays;
            size_t j = 0;
            for (unsigned a : s_arrivals) {
                while (j < s_pushes[0].size() && ((s_pushes[0][j] & 1) != (a & 1) || s_pushes[0][j] < a)) j++;
                if (j < s_pushes[0].size()) delays[static_cast<int>((s_pushes[0][j] / 2 - a / 2) / 2)]++;
            }
            for (auto [d, n] : delays) log::info("BOTTEST clicks that acted {} ticks after coming in: {}", d, n);
        } else {
            compare(s_run);
        }
        if (++s_run < 5) return startRun(this);
        finish();
        this->onQuit();
    }
};

class $modify(BotTestMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        std::error_code ec;
        auto trigger = saveDir() / "selftest-bot.txt";
        if (s_active || !std::filesystem::exists(trigger, ec)) return true;
        {
            std::ifstream in(trigger);
            std::string mode;
            in >> mode;
            s_keys = mode == "keys" || mode == "keysup";
            s_key = mode == "keysup" ? KEY_Up : KEY_Space;
        }
        std::filesystem::remove(trigger, ec);
        auto level = GameLevelManager::get()->getMainLevel(20, false);
        if (!level) return true;
        level->retain();
        s_level = level;
        for (auto& h : cmm::all()) s_saved.push_back({h.id, h.enabled});
        s_oldFps = cmm::val<float>("fpsbypass.value", 240.f);
        // Noclip without its death limits, so the test never dies.
        s_oldMaxDeaths = cmm::val<int>("noclip.maxdeaths", 0);
        s_oldMinAcc = cmm::val<float>("noclip.minacc", 0.f);
        cmm::val<int>("noclip.maxdeaths", 0) = 0;
        cmm::val<float>("noclip.minacc", 0.f) = 0.f;
        for (auto& p : s_path) p.clear();
        for (auto& p : s_pushes) p.clear();
        s_arrivals.clear();
        setHack("noclip", true);
        setHack("cbf", false);
        setHack("vsync", false);
        s_active = true;
        s_run = 0;
        log::info("BOTTEST start ({})", s_keys ? "clicks through GD's keyboard handler" : "clicks handed to GD directly");
        // Its clicks land exactly between frames: the bot's later timing.
        s_oldSooner = cmm::val<bool>("bot.sooner", true);
        cmm::val<bool>("bot.sooner", true) = false;
        Loader::get()->queueInMainThread([] {
            CCDirector::get()->replaceScene(PlayLayer::scene(s_level, false));
            Loader::get()->queueInMainThread([] {
                if (auto pl = PlayLayer::get()) startRun(pl);
            });
        });
        return true;
    }
};
