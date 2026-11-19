// Developer test: a macro recorded in practice mode with a checkpoint
// respawn, played back in normal mode. Only runs if "selftest-practice.txt"
// exists in the mod's save folder (deleted when it starts). On a level of
// spike groups it:
//   1. records in practice mode: jumps each group in time, but misses group 3
//      once on purpose, dies and respawns at a checkpoint placed before it
//   2. plays the macro in normal mode from the start (no noclip)
// and logs ("PRACTEST") whether the bot gets past the last group.
#include <core/Hacks.hpp>
#include <features/Replay.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <filesystem>
#include <fstream>

using namespace geode::prelude;

namespace {
    bool s_active = false;
    int s_run = 0;  // 0 record (practice), 1 play (normal)
    int s_group = 0, s_misses = 0, s_deaths = 0;
    bool s_checkpointPlaced = false, s_pressed = false;
    // "hold" in the trigger: on the first try, press before the checkpoint,
    // let go after it, then die (the respawn drops that release).
    bool s_hold = false, s_holdPressed = false, s_holdReleased = false;
    bool s_noRespawnFix = false;
    GJGameLevel* s_level = nullptr;
    std::vector<std::pair<std::string, bool>> s_saved;
    int s_oldMaxDeaths = 0;
    constexpr float kFirst = 900.f, kGap = 450.f;
    constexpr int kGroups = 8;

    std::filesystem::path saveDir() { return std::filesystem::path(Mod::get()->getSaveDir().native()); }
    void setHack(std::string_view id, bool on) { if (auto h = cmm::find(id); h && h->enabled != on) h->enabled = on; }
    float groupX(int g) { return kFirst + kGap * g; }

    void finish(PlayLayer* pl, char const* result) {
        log::info("PRACTEST {}", result);
        for (auto& [id, on] : s_saved) setHack(id, on);
        cmm::val<int>("noclip.maxdeaths", 0) = s_oldMaxDeaths;
        cmm::val<bool>("bot.respawnfix", true) = true;
        cmm::replay::clearBot();
        std::error_code ec;
        std::filesystem::remove(saveDir() / "macros" / "cmm-practest.txt", ec);
        s_active = false;
        log::info("PRACTEST DONE");
        if (pl) pl->onQuit();
    }
}

class $modify(PracTestLayer, PlayLayer) {
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool was = m_playerDied;
        PlayLayer::destroyPlayer(player, object);
        if (s_active && !was && m_playerDied && object != m_anticheatSpike) {
            s_deaths++;
            log::info("PRACTEST run {}: died at x {:.0f}", s_run, m_player1->getPositionX());
            if (s_run == 0) s_misses = 1;  // the miss on purpose happened
            if (s_run == 1) finish(this, fmt::format("FAILED: playback died at x {:.0f} (group {} of {})", m_player1->getPositionX(),
                std::max(0, static_cast<int>((m_player1->getPositionX() - kFirst + kGap / 2) / kGap)), kGroups).c_str());
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!s_active || PlayLayer::get() != this || m_playerDied) return;
        if (m_gameState.m_currentProgress == 0) return;
        float x = m_player1->getPositionX();
        if (s_run == 0) {
            if (!m_isPracticeMode) { this->togglePracticeMode(true); return; }
            // A checkpoint between groups 2 and 3.
            if (!s_checkpointPlaced && x > groupX(2) + 120.f) {
                this->markCheckpoint();
                s_checkpointPlaced = true;
                log::info("PRACTEST checkpoint at x {:.0f}", x);
            }
            // Which group is next.
            while (s_group < kGroups && x > groupX(s_group) + 100.f) { s_group++; s_pressed = false; }
            if (s_group >= kGroups) {
                bool saved = cmm::replay::saveCurrent("cmm-practest");
                log::info("PRACTEST recorded {} clicks with {} death(s) (saved {})", cmm::replay::botInputs(), s_deaths, saved);
                setHack("bot.record", false);
                cmm::replay::loadIntoBot("cmm-practest");
                setHack("bot.play", true);
                s_run = 1;
                s_group = 0;
                this->togglePracticeMode(false);
                this->resetLevel();
                return;
            }
            if (s_hold && s_misses == 0) {
                if (!s_holdPressed && x > groupX(2) + 60.f) { this->handleButton(true, 1, true); s_holdPressed = true; }
                if (s_holdPressed && !s_holdReleased && x > groupX(2) + 200.f) { this->handleButton(false, 1, true); s_holdReleased = true; }
            }
            // Group 3 is missed once on purpose.
            bool miss = s_group == 3 && s_misses == 0;
            if (!s_pressed && !miss && x > groupX(s_group) - 40.f) {
                this->handleButton(true, 1, true);
                this->handleButton(false, 1, true);
                s_pressed = true;
            }
            return;
        }
        // Playback: made it past the last group?
        if (x > groupX(kGroups - 1) + 200.f) finish(this, "PASSED: playback got past every group");
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (s_active && s_run == 0) {
            s_pressed = false;
            s_group = 0;
            log::info("PRACTEST respawned at x {:.0f}", m_player1->getPositionX());
        }
    }
};

class $modify(PracTestMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        std::error_code ec;
        auto trigger = saveDir() / "selftest-practice.txt";
        if (s_active || !std::filesystem::exists(trigger, ec)) return true;
        {
            std::ifstream in(trigger);
            std::string word;
            in >> word;
            s_hold = word == "hold" || word == "holdoff";
            // "holdoff": the same without the respawn handling, to compare.
            s_noRespawnFix = word == "holdoff";
            if (s_noRespawnFix) cmm::val<bool>("bot.respawnfix", true) = false;
        }
        std::filesystem::remove(trigger, ec);
        s_holdPressed = s_holdReleased = false;
        if (s_hold) log::info("PRACTEST holding across the checkpoint, letting go after it");
        // Flat ground and sets of three spikes.
        std::string raw = "kA2,0,kA4,0;";
        for (int g = 0; g < kGroups; g++) {
            int x = static_cast<int>(groupX(g));
            for (int i = 0; i < 3; i++) raw += fmt::format("1,8,2,{},3,15;", x + 30 * i);
        }
        raw += "1,1,2,9000,3,15;";
        auto level = GJGameLevel::create();
        level->retain();
        s_level = level;
        level->m_levelName = "CMM Practice Test";
        level->m_levelString = ZipUtils::compressString(raw, false, 0, 0);
        level->m_levelType = GJLevelType::Editor;
        for (auto& h : cmm::all()) s_saved.push_back({h.id, h.enabled});
        s_oldMaxDeaths = cmm::val<int>("noclip.maxdeaths", 0);
        setHack("noclip", false);
        setHack("cbf", false);
        setHack("framecounter", false);
        setHack("bot.play", false);
        setHack("instantrestart", true);
        cmm::replay::clearBot();
        setHack("bot.record", true);
        s_active = true;
        s_run = 0;
        s_group = s_misses = s_deaths = 0;
        s_checkpointPlaced = s_pressed = false;
        Loader::get()->queueInMainThread([] { CCDirector::get()->replaceScene(PlayLayer::scene(s_level, false)); });
        return true;
    }
};
