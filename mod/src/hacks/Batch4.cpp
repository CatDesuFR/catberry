// Fourth batch of modules from QOLMod / Eclipse's feature lists, written from scratch.
#include <core/Hacks.hpp>
#include <core/Log.hpp>
#include <features/Stats.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GameStatsManager.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// Chest Bypass: secret and special chests count as unlocked.
class $modify(B4Stats, GameStatsManager) {
    bool isSecretChestUnlocked(int id) {
        return cmm::on("chestbypass") || GameStatsManager::isSecretChestUnlocked(id);
    }
    bool isSpecialChestUnlocked(std::string key) {
        return cmm::on("chestbypass") || GameStatsManager::isSpecialChestUnlocked(std::move(key));
    }
};

class $modify(B4PlayLayer, PlayLayer) {
    // No Object Fade: objects GD fades out (or hides as "invisible") stay fully visible.
    float opacityForObject(GameObject* object) {
        float value = PlayLayer::opacityForObject(object);
        if (!cmm::on("noobjectfade")) return value;
        return 1.f;
    }
};

// End Screen Stats: noclip deaths/accuracy and where the run started.
class $modify(B4EndLevel, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        if (!cmm::on("endscreenstats")) return;
        auto pl = PlayLayer::get();
        if (!pl) return;
        std::string text;
        if (cmm::on("noclip")) {
            text = fmt::format("Noclip: {} deaths, {:.2f}% accuracy", cmm::stats::noclipDeaths, cmm::stats::noclipAccuracy());
        }
        text += fmt::format("{}Jumps: {}  Attempt time: {:.1f}s", text.empty() ? "" : "\n", pl->m_jumps, pl->m_attemptTime);
        auto label = CCLabelBMFont::create(text.c_str(), "goldFont.fnt");
        label->setScale(0.45f);
        label->setAlignment(kCCTextAlignmentCenter);
        auto win = CCDirector::get()->getWinSize();
        label->setPosition({win.width / 2.f, 38.f});
        label->setID("cats-mod-menu-end-stats");
        m_mainLayer->addChild(label, 10);
    }
};

namespace cmm {
    void registerBatch4() {
        using enum Tab;
        constexpr auto OK = PC::Allowed;
        constexpr auto NO = PC::NotAllowed;

        add({"chestbypass", "Chest Bypass", "Secret and special chests count as unlocked, so you can open them.", Bypass, OK});
        add({"instantrestart", "Instant Restart", "Respawn right away after dying.", Level, OK});
        add({"endscreenstats", "End Screen Stats", "Adds jumps, attempt time and (with noclip) deaths and accuracy to the level complete screen.", Level, OK});
        add({"noobjectfade", "No Object Fade", "Objects that fade out or are hidden stay fully visible.", Level, NO});

        Hack vsync{"vsync", "Vertical Sync", "Turns VSync on. Turn FPS Bypass off when you use this.", Global, OK};
        vsync.onToggle = [](bool on) {
            CCApplication::sharedApplication()->toggleVerticalSync(on);
            GameManager::get()->setGameVariable("0030", on);
        };
        add(std::move(vsync));
    }
}
