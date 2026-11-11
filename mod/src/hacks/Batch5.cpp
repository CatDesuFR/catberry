// Fifth batch of modules from QOLMod / Eclipse's feature lists, written from scratch.
#include <core/Hacks.hpp>
#include <core/Log.hpp>
#include <features/Sim.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/GameStatsManager.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Hitbox Multiplier: scales the player's, hazards' and solids' hitboxes.
// getObjectRect() is called very often, so the settings are read once a frame.
// ---------------------------------------------------------------------------
namespace {
    bool s_hbOn = false;
    float s_hbPlayer = 1.f, s_hbHazard = 1.f, s_hbSolid = 1.f;

    // The function returns a reference, so scaled rects live in a small ring;
    // callers compare two rects at most, right away.
    CCRect s_ring[16];
    int s_ringPos = 0;

    void refreshHitboxSettings() {
        s_hbOn = cmm::on("hitboxmultiplier");
        s_hbPlayer = std::clamp(cmm::val<float>("hitboxmultiplier.player", 1.f), 0.1f, 5.f);
        s_hbHazard = std::clamp(cmm::val<float>("hitboxmultiplier.hazard", 1.f), 0.1f, 5.f);
        s_hbSolid = std::clamp(cmm::val<float>("hitboxmultiplier.solid", 1.f), 0.1f, 5.f);
    }

    CCRect const& scaled(CCRect const& r, float k) {
        auto& out = s_ring[s_ringPos];
        s_ringPos = (s_ringPos + 1) % 16;
        float w = r.size.width * k, h = r.size.height * k;
        out = CCRect(r.getMidX() - w / 2, r.getMidY() - h / 2, w, h);
        return out;
    }
}

class $modify(B5Object, GameObject) {
    CCRect const& getObjectRect() {
        auto const& r = GameObject::getObjectRect();
        if (!s_hbOn) return r;
        auto pl = PlayLayer::get();
        if (!pl) return r;
        auto self = static_cast<GameObject*>(this);
        if (self == pl->m_player1 || self == pl->m_player2 || cmm::sim::isScratch(self)) {
            return s_hbPlayer == 1.f ? r : scaled(r, s_hbPlayer);
        }
        if (m_objectType == GameObjectType::Hazard) return s_hbHazard == 1.f ? r : scaled(r, s_hbHazard);
        if (m_objectType == GameObjectType::Solid) return s_hbSolid == 1.f ? r : scaled(r, s_hbSolid);
        return r;
    }
};

// Unlock All Items: every shop / reward item counts as unlocked
// (music customiser, practice music sync and the rest).
class $modify(B5Stats, GameStatsManager) {
    bool isItemUnlocked(UnlockType type, int id) {
        if (cmm::on("unlockitems") && type == UnlockType::GJItem) return true;
        return GameStatsManager::isItemUnlocked(type, id);
    }
};

class $modify(B5PlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool dontCreateObjects) {
        refreshHitboxSettings();
        return PlayLayer::init(level, dontCreateObjects);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        refreshHitboxSettings();
        // Hide Pause Button
        if (m_uiLayer && m_uiLayer->m_pauseBtn) {
            bool hide = cmm::on("hidepausebutton");
            if (m_uiLayer->m_pauseBtn->isVisible() == hide) m_uiLayer->m_pauseBtn->setVisible(!hide);
        }
    }

    void onQuit() {
        s_hbOn = false;
        PlayLayer::onQuit();
    }
};

// "Safe Mode" on the level complete screen, so a safe-mode completion is obvious.
class $modify(B5EndLevel, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        auto pl = PlayLayer::get();
        if (!pl || !cmm::on("safemodeendscreen")) return;
        bool safe = cmm::on("safemode") || (cmm::on("autosafemode") && pl->m_isTestMode);
        if (!safe) return;
        auto label = CCLabelBMFont::create("Safe Mode", "bigFont.fnt");
        label->setColor({255, 90, 90});
        label->setScale(0.5f);
        auto win = CCDirector::get()->getWinSize();
        label->setPosition({win.width / 2.f, win.height - 30.f});
        label->setID("cats-mod-menu-safe-mode");
        m_mainLayer->addChild(label, 10);
    }
};

namespace cmm {
    void registerBatch5() {
        using enum Tab;
        constexpr auto OK = PC::Allowed;
        constexpr auto NO = PC::NotAllowed;

        add({"hitboxmultiplier", "Hitbox Multiplier", "Makes hitboxes bigger or smaller: player, hazards and solids separately.", Level, NO,
            false, [] {
                optFloat("Player", "hitboxmultiplier.player", 1.f, 0.1f, 5.f, "%.2fx");
                optFloat("Hazards", "hitboxmultiplier.hazard", 1.f, 0.1f, 5.f, "%.2fx");
                optFloat("Solids", "hitboxmultiplier.solid", 1.f, 0.1f, 5.f, "%.2fx");
            }});
        add({"unlockitems", "Unlock All Items", "Every shop and reward item counts as unlocked (music customiser, practice music sync...).", Bypass, OK});
        add({"hidepausebutton", "Hide Pause Button", "Hides the pause button in levels.", Level, OK});
        add({"safemodeendscreen", "Safe Mode on End Screen", "Writes \"Safe Mode\" on the level complete screen when the run couldn't save.", Global, OK});
    }
}
