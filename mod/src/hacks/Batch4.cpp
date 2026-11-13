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

// End Screen Stats: clicks, how long this attempt took and (with noclip)
// deaths and accuracy, on a line under GD's own stats.
class $modify(B4EndLevel, EndLevelLayer) {
    // No End Shake: the box appears in place instead of dropping and bouncing in.
    void showLayer(bool instant) {
        EndLevelLayer::showLayer(instant || cmm::on("noendshake"));
    }

    void customSetup() {
        EndLevelLayer::customSetup();
        if (!cmm::on("endscreenstats")) return;
        auto pl = PlayLayer::get();
        if (!pl || !m_mainLayer) return;
        std::string text = fmt::format("Clicks: {}   Attempt: {:.2f}s", cmm::stats::attemptClicks, pl->m_gameState.m_levelTime);
        if (cmm::on("noclip") || cmm::stats::noclipDeaths > 0) {
            text += fmt::format("\nNoclip: {} deaths, {:.2f}% accuracy", cmm::stats::noclipDeaths, cmm::stats::noclipAccuracy());
        }
        auto label = CCLabelBMFont::create(text.c_str(), "goldFont.fnt");
        label->setScale(0.5f);
        label->setAlignment(kCCTextAlignmentCenter);
        label->setID("cats-mod-menu-end-stats");
        // Under the lowest of GD's stat lines (attempts / jumps / time).
        CCNode* lowest = nullptr;
        for (auto child : CCArrayExt<CCNode*>(m_mainLayer->getChildren())) {
            auto bm = typeinfo_cast<CCLabelBMFont*>(child);
            if (!bm || !bm->isVisible()) continue;
            std::string s = bm->getString();
            if (s.rfind("Attempts", 0) != 0 && s.rfind("Jumps", 0) != 0 && s.rfind("Time", 0) != 0) continue;
            if (!lowest || bm->getPositionY() < lowest->getPositionY()) lowest = bm;
        }
        auto win = CCDirector::get()->getWinSize();
        if (lowest) label->setPosition({lowest->getPositionX(), lowest->getPositionY() - lowest->getScaledContentSize().height / 2.f - 12.f});
        else label->setPosition({win.width / 2.f, win.height / 2.f - 20.f});
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
