// Batch 2 hacks: Respawn Delay, No Spider Dash, Auto Kill, Frame Stepper,
// Force Platformer, No/Instant Mirror, Auto LDM, confirm dialogs,
// Hide Level Complete VFX, Level Edit, Verify Bypass, No (C) Mark.
#include <core/Hacks.hpp>
#include <unordered_set>
#include <vector>
#include <core/Log.hpp>
#include <features/Stats.hpp>
#include <Geode/modify/CCDelayTime.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

using namespace geode::prelude;

namespace {
    bool s_inDestroyPlayer = false;
    // Set while we run the real action after the user confirmed, so the
    // hook doesn't ask again (calling the function goes back through the hook).
    bool s_confirmed = false;
    int s_stepRequests = 0;
}

namespace cmm::more {
    void requestFrameStep() {
        s_stepRequests++;
    }
}

// ---------------- Respawn Delay ----------------
// GD schedules the respawn with a CCDelayTime inside destroyPlayer.
class $modify(CMMDelay, CCDelayTime) {
    static CCDelayTime* create(float duration) {
        if (s_inDestroyPlayer && cmm::on("instantrestart")) duration = 0.f;
        else if (s_inDestroyPlayer && cmm::on("respawndelay")) duration = cmm::val<float>("respawndelay.value", 0.5f);
        return CCDelayTime::create(duration);
    }
};

class $modify(CMMMorePlayLayer, PlayLayer) {
    struct Fields {
        bool autoKilled = false;
    };

    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (level && cmm::on("autoldm")) level->m_lowDetailModeToggled = true;
        if (!PlayLayer::init(level, dontCreateObjects)) return false;

        if (cmm::on("forceplatformer")) {
            m_isPlatformer = true;
            for (auto p : {m_player1, m_player2}) {
                if (p) p->togglePlatformerMode(true);
            }
        }
        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        s_inDestroyPlayer = true;
        PlayLayer::destroyPlayer(player, object);
        s_inDestroyPlayer = false;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        m_fields->autoKilled = false;
    }

    // GD's complete effect also starts what brings up the end screen, so it
    // always runs; with Hide Level Complete VFX only the effect objects it
    // just added are hidden (their actions, like opening the end screen,
    // keep going).
    void showCompleteEffect() {
        if (!cmm::on("hidecompletevfx")) return PlayLayer::showCompleteEffect();
        std::vector<CCNode*> layers{this, m_objectLayer};
        std::vector<std::unordered_set<CCNode*>> before;
        for (auto layer : layers) {
            std::unordered_set<CCNode*> kids;
            if (layer && layer->getChildren()) {
                for (auto child : CCArrayExt<CCNode*>(layer->getChildren())) kids.insert(child);
            }
            before.push_back(std::move(kids));
        }
        PlayLayer::showCompleteEffect();
        for (size_t i = 0; i < layers.size(); i++) {
            auto layer = layers[i];
            if (!layer || !layer->getChildren()) continue;
            for (auto child : CCArrayExt<CCNode*>(layer->getChildren())) {
                if (!before[i].count(child)) child->setVisible(false);
            }
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto f = m_fields.self();
        if (!cmm::on("autokill") || f->autoKilled || m_playerDied || m_isPlatformer) return;
        if (this->getCurrentPercent() >= cmm::val<float>("autokill.percent", 50.f)) {
            f->autoKilled = true;
            cmm::stats::forceKill = true;
            this->destroyPlayer(m_player1, nullptr);
            cmm::stats::forceKill = false;
        }
    }
};

class $modify(CMMMorePlayer, PlayerObject) {
    void playSpiderDashEffect(CCPoint from, CCPoint to) {
        if (cmm::on("nospiderdash")) return;
        PlayerObject::playSpiderDashEffect(from, to);
    }
};

class $modify(CMMMoreGameLayer, GJBaseGameLayer) {
    void toggleFlipped(bool flip, bool noEffects) {
        if (cmm::on("nomirror")) return;
        GJBaseGameLayer::toggleFlipped(flip, noEffects || cmm::on("instantmirror"));
    }

    // Frame Stepper: the level is frozen, and each step key press runs one tick.
    void update(float dt) {
        auto pl = PlayLayer::get();
        if (!cmm::on("framestepper") || !pl || static_cast<GJBaseGameLayer*>(pl) != this || pl->m_isPaused) {
            GJBaseGameLayer::update(dt);
            return;
        }
        if (s_stepRequests > 0) {
            s_stepRequests--;
            GJBaseGameLayer::update(1.f / 240.f);
        }
    }
};

// ---------------- Pause menu: confirms + Level Edit ----------------
class $modify(CMMMorePause, PauseLayer) {
    void customSetup() {
        auto pl = PlayLayer::get();
        auto level = pl ? pl->m_level : nullptr;
        bool fake = level && cmm::on("leveledit") && level->m_levelType != GJLevelType::Editor;
        auto old = level ? level->m_levelType : GJLevelType::Default;
        if (fake) level->m_levelType = GJLevelType::Editor;
        PauseLayer::customSetup();
        if (fake) level->m_levelType = old;
    }

    void onEdit(CCObject* sender) {
        auto pl = PlayLayer::get();
        auto level = pl ? pl->m_level : nullptr;
        bool fake = level && cmm::on("leveledit") && level->m_levelType != GJLevelType::Editor;
        auto old = level ? level->m_levelType : GJLevelType::Default;
        if (fake) level->m_levelType = GJLevelType::Editor;
        PauseLayer::onEdit(sender);
        if (fake) level->m_levelType = old;
    }

    void onRestart(CCObject* sender) {
        if (s_confirmed || !cmm::on("confirmrestart")) return PauseLayer::onRestart(sender);
        Ref<PauseLayer> self = this;
        createQuickPopup("Restart", "Restart the level?", "Cancel", "Restart", [self, sender](FLAlertLayer*, bool yes) {
            if (!yes) return;
            s_confirmed = true;
            self->onRestart(sender);
            s_confirmed = false;
        });
    }

    void onRestartFull(CCObject* sender) {
        if (s_confirmed || !cmm::on("confirmrestart")) return PauseLayer::onRestartFull(sender);
        Ref<PauseLayer> self = this;
        createQuickPopup("Full Restart", "Restart the level from the start?", "Cancel", "Restart", [self, sender](FLAlertLayer*, bool yes) {
            if (!yes) return;
            s_confirmed = true;
            self->onRestartFull(sender);
            s_confirmed = false;
        });
    }

    void onPracticeMode(CCObject* sender) {
        if (s_confirmed || !cmm::on("confirmpractice")) return PauseLayer::onPracticeMode(sender);
        Ref<PauseLayer> self = this;
        createQuickPopup("Practice Mode", "Enter practice mode?", "Cancel", "Yes", [self, sender](FLAlertLayer*, bool yes) {
            if (!yes) return;
            s_confirmed = true;
            self->onPracticeMode(sender);
            s_confirmed = false;
        });
    }

    void onQuit(CCObject* sender) {
        if (s_confirmed || !cmm::on("confirmexit")) return PauseLayer::onQuit(sender);
        Ref<PauseLayer> self = this;
        createQuickPopup("Exit", "Leave the level?", "Cancel", "Exit", [self, sender](FLAlertLayer*, bool yes) {
            if (!yes) return;
            s_confirmed = true;
            self->onQuit(sender);
            s_confirmed = false;
        });
    }

    void onNormalMode(CCObject* sender) {
        if (s_confirmed || !cmm::on("confirmpractice")) return PauseLayer::onNormalMode(sender);
        Ref<PauseLayer> self = this;
        createQuickPopup("Normal Mode", "Leave practice mode?", "Cancel", "Yes", [self, sender](FLAlertLayer*, bool yes) {
            if (!yes) return;
            s_confirmed = true;
            self->onNormalMode(sender);
            s_confirmed = false;
        });
    }
};

// ---------------- Creator ----------------
class $modify(CMMMoreEditLevel, EditLevelLayer) {
    void onShare(CCObject* sender) {
        if (m_level) {
            if (cmm::on("verifybypass")) {
                m_level->m_isVerified = 1;
                m_level->m_isVerifiedRaw = true;
            }
            if (cmm::on("nocmark")) m_level->m_originalLevel = 0;
        }
        EditLevelLayer::onShare(sender);
    }
};
