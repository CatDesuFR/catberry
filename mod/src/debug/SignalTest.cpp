// Developer test of No Pad Signal / No Gravity Signal. Only runs if
// "selftest-signal.txt" exists in the mod's save folder (deleted when it
// starts). Plays Deadlocked with noclip, holding jump, for 40 s with both off, then 40 s with
// both on, and logs ("SIGNALTEST") how many pad/orb flashes and gravity arrow
// effects GD really drew.
#include <core/Hacks.hpp>
#include <Geode/modify/CCCircleWave.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <filesystem>

using namespace geode::prelude;

namespace {
    bool s_active = false;
    int s_run = 0, s_frames = 0, s_shines = 0, s_gravity = 0, s_events = 0;
    GJGameLevel* s_level = nullptr;
    std::vector<std::pair<std::string, bool>> s_saved;
    std::filesystem::path saveDir() { return std::filesystem::path(Mod::get()->getSaveDir().native()); }
    void setHack(std::string_view id, bool on) { if (auto h = cmm::find(id)) h->enabled = on; }
}

class $modify(SignalTestObject, GameObject) {
    static void onModify(auto& self) { (void)self.setHookPriority("GameObject::playShineEffect", Priority::Last); }
    void playShineEffect() {
        if (s_active) s_shines++;
        GameObject::playShineEffect();
    }
};

class $modify(SignalTestWave, CCCircleWave) {
    static CCCircleWave* create(float startRadius, float endRadius, float duration, bool fadeIn, bool easeOut) {
        auto w = CCCircleWave::create(startRadius, endRadius, duration, fadeIn, easeOut);
        return w;
    }
};

class $modify(SignalTestEvents, GJBaseGameLayer) {
    static void onModify(auto& self) { (void)self.setHookPriority("GJBaseGameLayer::gameEventTriggered", Priority::Last); }
    void gameEventTriggered(GJGameEvent event, int material, int playerID) {
        int e = static_cast<int>(event);
        if (s_active && e >= 7 && e <= 11) s_events++;
        GJBaseGameLayer::gameEventTriggered(event, material, playerID);
    }
};

class $modify(SignalTestLayer, PlayLayer) {
    static void onModify(auto& self) { (void)self.setHookPriority("PlayLayer::playGravityEffect", Priority::Last); }
    void playGravityEffect(bool flip) {
        if (s_active) s_gravity++;
        PlayLayer::playGravityEffect(flip);
    }
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!s_active || PlayLayer::get() != this) return;
        if (m_gameState.m_currentProgress == 0) return;
        // Hold jump (orbs get used); let go and press again now and then.
        if (s_frames % 120 == 0) this->handleButton(true, 1, true);
        if (s_frames % 120 == 110) this->handleButton(false, 1, true);
        if (++s_frames < 240 * 40) return;
        log::info("SIGNALTEST {}: {} orb/pad/gravity signals reached the level, {} gravity arrow effects drawn", s_run == 0 ? "both off" : "both on", s_events, s_gravity);
        s_frames = s_shines = s_gravity = s_events = 0;
        if (s_run++ == 0) {
            setHack("nopadsignal", true);
            setHack("nogravitysignal", true);
            this->resetLevel();
            return;
        }
        for (auto& [id, on] : s_saved) setHack(id, on);
        s_active = false;
        log::info("SIGNALTEST DONE");
        this->onQuit();
    }
};

class $modify(SignalTestMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        std::error_code ec;
        auto trigger = saveDir() / "selftest-signal.txt";
        if (s_active || !std::filesystem::exists(trigger, ec)) return true;
        std::filesystem::remove(trigger, ec);
        auto level = GameLevelManager::get()->getMainLevel(20, false);
        if (!level) return true;
        level->retain();
        s_level = level;
        for (auto& h : cmm::all()) s_saved.push_back({h.id, h.enabled});
        setHack("noclip", true);
        setHack("nopadsignal", false);
        setHack("nogravitysignal", false);
        setHack("bot.play", false);
        s_active = true;
        s_run = s_frames = s_shines = s_gravity = 0;
        Loader::get()->queueInMainThread([] { CCDirector::get()->replaceScene(PlayLayer::scene(s_level, false)); });
        return true;
    }
};
