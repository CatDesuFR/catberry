// Developer test of Hitboxes On Death in practice mode. Only runs if
// "selftest-death2.txt" exists in the mod's save folder (deleted when it
// starts). Plays main level 1 in practice mode without clicking, places a
// checkpoint early, and dies four times. After each death it logs ("DEATHTEST")
// how big GD's saved hitbox trail is and how much the hitbox layer draws, and
// saves a screenshot of the last death.
#include <core/Hacks.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <filesystem>

using namespace geode::prelude;

namespace {
    bool s_active = false;
    int s_deaths = 0, s_frames = 0, s_deadFrames = 0;
    bool s_checkpoint = false;
    GJGameLevel* s_level = nullptr;
    std::vector<std::pair<std::string, bool>> s_saved;

    std::filesystem::path saveDir() { return std::filesystem::path(Mod::get()->getSaveDir().native()); }

    void setHack(std::string_view id, bool on) {
        if (auto h = cmm::find(id); h && h->enabled != on) h->enabled = on;
    }

    void shot(char const* name) {
        auto size = CCDirector::get()->getWinSize();
        auto rt = CCRenderTexture::create(static_cast<int>(size.width), static_cast<int>(size.height));
        rt->begin();
        CCDirector::get()->getRunningScene()->visit();
        rt->end();
        auto img = rt->newCCImage(true);
        if (img) {
            img->saveToFile((saveDir() / name).string().c_str(), false);
            img->release();
        }
    }
}

class $modify(DeathTestLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!s_active || PlayLayer::get() != this) return;
        if (m_level != s_level) { s_active = false; return; }
        s_frames++;
        if (!m_isPracticeMode) { this->togglePracticeMode(true); return; }
        if (!s_checkpoint && m_player1->getPositionX() > 150.f) {
            this->markCheckpoint();
            s_checkpoint = true;
            log::info("DEATHTEST checkpoint at x {:.0f}", m_player1->getPositionX());
        }
        static int s_alive = 0;
        static bool s_jumped = false;
        if (!m_playerDied) {
            if (s_deadFrames > 0) { s_alive = 0; s_jumped = false; }
            s_deadFrames = 0;
            if (++s_alive == 30 && s_deaths > 0) {
                auto node = m_debugDrawNode;
                log::info("DEATHTEST 30 frames after respawn {}: hitbox layer {} vertices, visible {}, debug draw {}, extra nodes {}", s_deaths,
                    node ? static_cast<int>(node->m_nBufferCount) : -1, node && node->isVisible(), m_isDebugDrawEnabled, this->getChildrenCount());
                if (s_deaths == 3) shot("selftest-respawn.png");
            }
            // Attempts 2 and later jump the first spike at different places, so
            // they die further on.
            float x = m_player1->getPositionX();
            float jumpAt = 430.f + 15.f * s_deaths;
            if (s_deaths >= 1 && !s_jumped && x > jumpAt) {
                this->handleButton(true, 1, true);
                this->handleButton(false, 1, true);
                s_jumped = true;
            }
            return;
        }
        if (++s_deadFrames != 5) return;
        s_deaths++;
        auto node = m_debugDrawNode;
        log::info("DEATHTEST death {} at x {:.0f}: trail {} bytes (limit {}), hitbox layer {} vertices, visible {}", s_deaths,
            m_player1->getPositionX(), m_player1HitboxTrail.size(), m_player1HitboxTrailLimit,
            node ? static_cast<int>(node->m_nBufferCount) : -1, node && node->isVisible());
        if (s_deaths < 4) return;
        shot("selftest-death2.png");
        for (auto& [id, on] : s_saved) setHack(id, on);
        s_active = false;
        log::info("DEATHTEST DONE");
        this->onQuit();
    }
};

class $modify(DeathTestMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        std::error_code ec;
        auto trigger = saveDir() / "selftest-death2.txt";
        if (s_active || !std::filesystem::exists(trigger, ec)) return true;
        std::filesystem::remove(trigger, ec);
        auto level = GameLevelManager::get()->getMainLevel(1, false);
        if (!level) return true;
        level->retain();
        s_level = level;
        for (auto& h : cmm::all()) s_saved.push_back({h.id, h.enabled});
        setHack("noclip", false);
        setHack("bot.play", false);
        setHack("bot.record", false);
        setHack("showhitboxes", false);
        setHack("hitboxesondeath", true);
        setHack("instantrestart", false);
        s_active = true;
        s_deaths = 0;
        s_checkpoint = false;
        Loader::get()->queueInMainThread([] { CCDirector::get()->replaceScene(PlayLayer::scene(s_level, false)); });
        return true;
    }
};
