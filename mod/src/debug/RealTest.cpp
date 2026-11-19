// Developer test on a saved (downloaded) level with one of your macros. Only
// runs if "selftest-real.txt" exists in the mod's save folder (deleted when it
// starts). The file holds three lines: the level's name, the macro's name, and
// a mode:
//   preload  - Replay Bot + Frame Window Counter with the loading screen:
//              how many clicks it measures, how many are "?", whether the
//              macro dies, then plays 20 s and counts the markers
//   end      - plays the macro to the end with noclip: counts camera shakes
//              (No End Shake off, then on) and screenshots the end screen
//              with End Screen Stats
// Writes "REALTEST" lines to the Geode log; your settings are put back.
#include <core/Hacks.hpp>
#include <features/FrameWindow.hpp>
#include <features/Replay.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace geode::prelude;

namespace {
    bool s_active = false;
    std::string s_mode;
    GJGameLevel* s_level = nullptr;
    std::vector<std::pair<std::string, bool>> s_saved;
    int s_frames = 0, s_shakes = 0, s_blockedRun = 0, s_run = 0;
    bool s_loggedLoad = false;

    std::filesystem::path saveDir() { return std::filesystem::path(Mod::get()->getSaveDir().native()); }

    void setHack(std::string_view id, bool on) {
        if (auto h = cmm::find(id); h && h->enabled != on) h->enabled = on;
    }

    void restore() {
        for (auto& [id, on] : s_saved) setHack(id, on);
        s_saved.clear();
        cmm::replay::clearBot();
        s_active = false;
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

    // A downloaded level, or one of your own (created) levels.
    GJGameLevel* findSaved(std::string const& name) {
        if (auto levels = GameLevelManager::get()->getSavedLevels(false, 0)) {
            for (auto l : CCArrayExt<GJGameLevel*>(levels)) {
                if (l && std::string(l->m_levelName) == name) return l;
            }
        }
        if (auto local = LocalLevelManager::get()->m_localLevels) {
            for (auto l : CCArrayExt<GJGameLevel*>(local)) {
                if (l && std::string(l->m_levelName) == name) return l;
            }
        }
        if (auto online = GameLevelManager::get()->m_onlineLevels) {
            for (auto [key, obj] : CCDictionaryExt<std::string, GJGameLevel*>(online)) {
                if (obj && std::string(obj->m_levelName) == name) return obj;
            }
        }
        int n = 0;
        if (auto levels = GameLevelManager::get()->getSavedLevels(false, 0)) {
            for (auto l : CCArrayExt<GJGameLevel*>(levels)) if (l && n++ < 40) log::info("REALTEST saved level: {}", std::string(l->m_levelName));
        }
        return nullptr;
    }
}

// Counts camera shakes that get through (after No End Shake's own hook).
class $modify(RealTestShake, GJBaseGameLayer) {
    static void onModify(auto& self) { (void)self.setHookPriority("GJBaseGameLayer::shakeCamera", Priority::Last); }
    void shakeCamera(float duration, float strength, float interval) {
        if (s_active && s_mode == "end") s_shakes++;
        GJBaseGameLayer::shakeCamera(duration, strength, interval);
    }
};

class $modify(RealTestLayer, PlayLayer) {
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool was = m_playerDied;
        PlayLayer::destroyPlayer(player, object);
        if (!s_active || s_mode != "play" || was || !m_playerDied || object == m_anticheatSpike) return;
        log::info("REALTEST play: the bot died at {:.1f}% (x {:.0f}, progress {}), object {}", this->getCurrentPercent(),
            m_player1->getPositionX(), m_gameState.m_currentProgress, object ? object->m_objectID : -1);
        auto mode = [](PlayerObject* p) {
            if (!p) return std::string("none");
            return std::string(p->m_isShip ? "ship" : p->m_isDart ? "wave" : p->m_isBall ? "ball" : p->m_isBird ? "ufo" : p->m_isRobot ? "robot"
                : p->m_isSpider ? "spider" : p->m_isSwing ? "swing" : "cube");
        };
        log::info("REALTEST play: dead player {}, dual {}, p1 {} at ({:.1f},{:.1f}) yVel {:.3f}, p2 {} at ({:.1f},{:.1f}) yVel {:.3f}",
            player == m_player2 ? 2 : 1, m_gameState.m_isDualMode, mode(m_player1), m_player1->getPositionX(), m_player1->getPositionY(), m_player1->m_yVelocity,
            mode(m_player2), m_player2 ? m_player2->getPositionX() : 0.f, m_player2 ? m_player2->getPositionY() : 0.f, m_player2 ? m_player2->m_yVelocity : 0.0);
        restore();
        log::info("REALTEST DONE");
        this->onQuit();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!s_active || PlayLayer::get() != this) return;
        if (!m_level || std::string(m_level->m_levelName) != std::string(s_level->m_levelName)) { restore(); log::info("REALTEST stopped (left the level)"); return; }
        if (s_mode == "end" && !m_playerDied && !m_levelEndAnimationStarted) {
            // A few clicks, so the end screen has some to count.
            if (++s_frames % 150 == 60) this->handleButton(true, 1, true);
            if (s_frames % 150 == 70) this->handleButton(false, 1, true);
        }
        if (s_mode == "preload") {
            if (cmm::fw::loading()) return;
            if (!s_loggedLoad) {
                s_loggedLoad = true;
                s_frames = 0;
                log::info("REALTEST loading screen finished");
            }
            if (++s_frames < 240 * 20) return;
            log::info("REALTEST played 20 s, at {:.1f}%, dead {}", this->getCurrentPercent(), m_playerDied);
            restore();
            log::info("REALTEST DONE");
            this->onQuit();
        }
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (s_active && s_mode == "play") {
            log::info("REALTEST play: the bot finished the level");
            restore();
            log::info("REALTEST DONE");
            return;
        }
        if (!s_active || s_mode != "end") return;
        log::info("REALTEST run {} (No End Shake {}): level complete, {} camera shakes so far", s_run, s_run == 0 ? "off" : "on", s_shakes);
    }
};

class $modify(RealTestEnd, EndLevelLayer) {
    void showLayer(bool instant) {
        EndLevelLayer::showLayer(instant);
        if (!s_active || s_mode != "end") return;
        Ref<EndLevelLayer> self = this;
        // What moves while the end screen comes in: track positions for 2 s.
        struct Track : CCNode {
            Ref<EndLevelLayer> end;
            int frames = 0;
            float pl = 0, layer = 0, main = 0, scene = 0;
            CCPoint pl0, layer0, main0, scene0;
            void tick(float) {
                auto pl = PlayLayer::get();
                auto sc = CCDirector::get()->getRunningScene();
                auto dev = [](CCPoint a, CCPoint b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); };
                if (frames == 0) { pl0 = pl ? pl->getPosition() : CCPoint{}; layer0 = end->getPosition(); main0 = end->m_mainLayer->getPosition(); scene0 = sc->getPosition(); }
                if (pl) this->pl = std::max(this->pl, dev(pl->getPosition(), pl0));
                layer = std::max(layer, dev(end->getPosition(), layer0));
                scene = std::max(scene, dev(sc->getPosition(), scene0));
                if (++frames >= 480) {
                    log::info("REALTEST run {}: moved while the end screen came in: level layer {:.1f}, end layer {:.1f}, scene {:.1f}", s_run, this->pl, layer, scene);
                    this->unscheduleAllSelectors();
                    this->removeFromParent();
                }
            }
        };
        auto track = new Track();
        track->autorelease();
        track->end = this;
        CCDirector::get()->getRunningScene()->addChild(track);
        track->schedule(schedule_selector(Track::tick));
        // Let the shake and drop-in finish, then look.
        self->runAction(CCSequence::create(CCDelayTime::create(2.6f), CallFuncExt::create([self] {
            log::info("REALTEST run {} (No End Shake {}): {} camera shakes in total", s_run, s_run == 0 ? "off" : "on", s_shakes);
            shot(s_run == 0 ? "selftest-end-off.png" : "selftest-end-on.png");
            if (s_run == 0) {
                s_run = 1;
                s_shakes = 0;
                setHack("noendshake", true);
                if (auto pl = PlayLayer::get()) {
                    self->removeFromParent();
                    pl->resetLevel();
                }
                return;
            }
            restore();
            log::info("REALTEST DONE");
            if (auto pl = PlayLayer::get()) pl->onQuit();
        }), nullptr));
    }
};

class $modify(RealTestMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        std::error_code ec;
        auto trigger = saveDir() / "selftest-real.txt";
        if (s_active || !std::filesystem::exists(trigger, ec)) return true;
        std::string levelName, macro;
        {
            std::ifstream in(trigger);
            std::getline(in, levelName);
            std::getline(in, macro);
            std::getline(in, s_mode);
        }
        std::filesystem::remove(trigger, ec);
        GJGameLevel* level = nullptr;
        if (levelName.rfind("@file:", 0) == 0) {
            // A level's object string from a file in testlevels/ (downloaded for tests).
            std::ifstream in(saveDir() / "testlevels" / levelName.substr(6));
            std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (!raw.empty()) {
                level = GJGameLevel::create();
                level->m_levelName = levelName.substr(6);
                level->m_levelString = ZipUtils::compressString(raw, false, 0, 0);
                level->m_levelType = GJLevelType::Editor;
            }
        } else if (levelName.rfind("@main", 0) == 0) {
            level = GameLevelManager::get()->getMainLevel(std::atoi(levelName.c_str() + 5), false);
        } else if (levelName == "@short") {
            // A short level: flat ground, the end a few seconds in.
            level = GJGameLevel::create();
            level->m_levelName = "CMM Short Test";
            level->m_levelString = ZipUtils::compressString("kA2,0,kA4,0;1,1,2,900,3,15;", false, 0, 0);
            level->m_levelType = GJLevelType::Editor;
        } else {
            level = findSaved(levelName);
        }
        if (!level) { log::error("REALTEST: no saved level called {}", levelName); return true; }
        level->retain();
        s_level = level;
        for (auto& h : cmm::all()) s_saved.push_back({h.id, h.enabled});
        bool loaded = cmm::replay::loadIntoBot(macro);
        if (s_mode == "playnofix") {
            auto& m = const_cast<std::vector<cmm::replay::Input>&>(cmm::replay::botMacro());
            for (auto& in : m) in.fix.ok = in.fix.ok2 = false;
            s_mode = "play";
        }
        setHack("bot.record", false);
        setHack("bot.play", true);
        setHack("cbf", false);
        if (s_mode == "play") {
            setHack("framecounter", false);
            setHack("noclip", false);
        } else if (s_mode == "preload") {
            setHack("framecounter", true);
            setHack("noclip", false);
            setHack("cbf", false);
        } else {
            setHack("framecounter", false);
            setHack("noclip", true);
            setHack("noendshake", false);
            setHack("endscreenstats", true);
            setHack("hidecompletevfx", false);  // GD's own end effects, to see its shake
        }
        s_active = true;
        s_frames = s_shakes = s_run = 0;
        s_loggedLoad = false;
        log::info("REALTEST start: level {} (id {}), macro {} ({}, {} clicks), mode {}", levelName, level->m_levelID.value(), macro,
            loaded ? "loaded" : "NOT loaded", cmm::replay::botInputs(), s_mode);
        Loader::get()->queueInMainThread([] { CCDirector::get()->replaceScene(PlayLayer::scene(s_level, false)); });
        return true;
    }
};
