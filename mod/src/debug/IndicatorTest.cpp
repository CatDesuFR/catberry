// Developer test of the cheat indicator. Only runs if "selftest-indicator.txt"
// exists in the mod's save folder (deleted when it starts). On main level 1:
// noclip on until it saves the player from the first spike, then noclip off,
// then a restart; logs ("INDTEST") the indicator at each step.
#include <core/Hacks.hpp>
#include <core/Indicator.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <filesystem>

using namespace geode::prelude;

namespace {
    bool s_active = false;
    int s_step = 0, s_wait = 0;
    GJGameLevel* s_level = nullptr;
    bool s_oldNoclip = false;
    std::filesystem::path saveDir() { return std::filesystem::path(Mod::get()->getSaveDir().native()); }
    char const* name(cmm::PC pc) { return pc == cmm::PC::NotAllowed ? "red" : "green"; }
}

class $modify(IndTestLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!s_active || PlayLayer::get() != this) return;
        auto noclip = cmm::find("noclip");
        float x = m_player1->getPositionX();
        if (s_step == 0 && x > 560.f) {  // past the first spike: noclip saved us
            log::info("INDTEST noclip on, after it saved you: {}", name(cmm::indicator::attempt()));
            if (noclip) noclip->enabled = false;
            s_step = 1;
            s_wait = 0;
        } else if (s_step == 1 && ++s_wait == 30) {
            log::info("INDTEST noclip turned off, same attempt: {}", name(cmm::indicator::attempt()));
            this->resetLevel();
            s_step = 2;
            s_wait = 0;
        } else if (s_step == 2 && ++s_wait == 30) {
            log::info("INDTEST after restarting: {}", name(cmm::indicator::attempt()));
            if (noclip) noclip->enabled = s_oldNoclip;
            s_active = false;
            log::info("INDTEST DONE");
            this->onQuit();
        }
    }
};

class $modify(IndTestMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        std::error_code ec;
        auto trigger = saveDir() / "selftest-indicator.txt";
        if (s_active || !std::filesystem::exists(trigger, ec)) return true;
        std::filesystem::remove(trigger, ec);
        auto level = GameLevelManager::get()->getMainLevel(1, false);
        if (!level) return true;
        level->retain();
        s_level = level;
        auto noclip = cmm::find("noclip");
        s_oldNoclip = noclip && noclip->enabled;
        if (noclip) noclip->enabled = true;
        s_active = true;
        s_step = 0;
        Loader::get()->queueInMainThread([] { CCDirector::get()->replaceScene(PlayLayer::scene(s_level, false)); });
        return true;
    }
};
