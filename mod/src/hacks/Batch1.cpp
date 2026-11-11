// Modules from QOLMod / Eclipse's feature lists, written from scratch.
#include <core/Hacks.hpp>
#include <core/Log.hpp>
#include <features/Stats.hpp>
#include <features/WorldOverlay.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace {
    bool inLevel(PlayerObject* p) {
        auto pl = PlayLayer::get();
        return pl && (p == pl->m_player1 || p == pl->m_player2);
    }

    // Pause countdown state.
    bool s_resumeNow = false;
    bool s_counting = false;
}

class $modify(B1PlayLayer, PlayLayer) {
    struct Fields {
        Ref<CCDrawNode> tracers;
        std::vector<GameObject*> coins;
        Ref<CCLabelBMFont> countdown;
    };

    // Freeze Attempts
    void updateAttempts() {
        if (cmm::on("freezeattempts")) return;
        PlayLayer::updateAttempts();
    }

    // Instant Complete
    void startGame() {
        PlayLayer::startGame();
        if (cmm::on("instantcomplete")) {
            this->runAction(CCSequence::create(CCDelayTime::create(0.1f), CCCallFunc::create(this, callfunc_selector(B1PlayLayer::completeNow)), nullptr));
        }
    }

    void completeNow() {
        if (!m_playerDied && !m_hasCompletedLevel) this->levelComplete();
    }

    // Pause Countdown: after unpausing, count 3-2-1 before the game moves.
    void resume() {
        if (!cmm::on("pausecountdown") || s_resumeNow) {
            PlayLayer::resume();
            return;
        }
        if (s_counting) return;
        s_counting = true;
        auto f = m_fields.self();
        int seconds = std::clamp(cmm::val<int>("pausecountdown.seconds", 3), 1, 10);
        auto win = CCDirector::get()->getWinSize();
        auto label = CCLabelBMFont::create(std::to_string(seconds).c_str(), "bigFont.fnt");
        label->setPosition(win / 2);
        label->setZOrder(2000);
        this->addChild(label);
        f->countdown = label;
        auto actions = CCArray::create();
        for (int i = seconds; i >= 1; i--) {
            actions->addObject(CCCallFuncO::create(this, callfuncO_selector(B1PlayLayer::countdownStep), CCInteger::create(i)));
            actions->addObject(CCDelayTime::create(1.f));
        }
        actions->addObject(CCCallFunc::create(this, callfunc_selector(B1PlayLayer::countdownDone)));
        label->runAction(CCSequence::create(actions));
    }

    void countdownStep(CCObject* n) {
        auto f = m_fields.self();
        if (f->countdown) f->countdown->setString(std::to_string(static_cast<CCInteger*>(n)->getValue()).c_str());
    }

    void countdownDone() {
        auto f = m_fields.self();
        if (f->countdown) f->countdown->removeFromParent();
        f->countdown = nullptr;
        s_counting = false;
        s_resumeNow = true;
        this->resume();
        s_resumeNow = false;
    }

    void pauseGame(bool unfocused) {
        // Pausing again during the countdown cancels it.
        auto f = m_fields.self();
        if (s_counting && f->countdown) {
            f->countdown->stopAllActions();
            f->countdown->removeFromParent();
            f->countdown = nullptr;
            s_counting = false;
        }
        PlayLayer::pauseGame(unfocused);
    }

    void onQuit() {
        s_counting = false;
        PlayLayer::onQuit();
    }

    // Coin Tracers
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        auto f = m_fields.self();
        f->coins.clear();
        if (m_objects) {
            for (auto o : CCArrayExt<GameObject*>(m_objects)) {
                if (o && (o->m_objectType == GameObjectType::SecretCoin || o->m_objectType == GameObjectType::UserCoin)) f->coins.push_back(o);
            }
        }
        auto node = CCDrawNode::create();
        node->setID("cats-mod-menu-coin-tracers");
        this->addChild(node, 997);
        f->tracers = node;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto f = m_fields.self();

        // No Wave Trail / No Trail Blending
        for (auto p : {m_player1, m_player2}) {
            if (!p) continue;
            if (cmm::on("nowavetrail") && p->m_waveTrail) p->m_waveTrail->setVisible(false);
            if (cmm::on("notrailblending") && p->m_regularTrail) p->m_regularTrail->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
        }

        if (!f->tracers) return;
        f->tracers->clear();
        if (!cmm::on("cointracers") || f->coins.empty()) return;
        cmm::overlay::syncTo(f->tracers, m_objectLayer, this);
        auto from = m_player1->getPosition();
        for (auto coin : f->coins) {
            // Collected coins are hidden by GD.
            if (coin->m_isDisabled || coin->m_isDisabled2 || (coin->getParent() && !coin->isVisible())) continue;
            f->tracers->drawSegment(from, coin->getPosition(), 0.6f, {1.f, 0.85f, 0.2f, 0.8f});
        }
    }
};

class $modify(B1GameLayer, GJBaseGameLayer) {
    // No Camera Shake (all of it, not just the end shake)
    void shakeCamera(float duration, float strength, float interval) {
        if (cmm::on("nocamerashake") && PlayLayer::get() == static_cast<GJBaseGameLayer*>(this)) return;
        GJBaseGameLayer::shakeCamera(duration, strength, interval);
    }
};

class $modify(B1Player, PlayerObject) {
    // No Rotation
    void setRotation(float rotation) {
        if (cmm::on("norotation") && inLevel(this)) rotation = 0.f;
        PlayerObject::setRotation(rotation);
    }

    // No Ghost Trail / Force Ghost Trail
    void toggleGhostEffect(GhostType type) {
        if (inLevel(this)) {
            if (cmm::on("noghosttrail")) type = GhostType::Disabled;
            else if (cmm::on("forceghosttrail")) type = GhostType::Enabled;
        }
        PlayerObject::toggleGhostEffect(type);
    }

    // Force Show Player (ignore hide-player triggers)
    void toggleVisibility(bool visible) {
        if (cmm::on("forceshowplayer") && inLevel(this)) visible = true;
        PlayerObject::toggleVisibility(visible);
    }
};

namespace cmm {
    void registerBatch1() {
        using enum Tab;
        constexpr auto OK = PC::Allowed;
        constexpr auto SOMETIMES = PC::Sometimes;
        constexpr auto NO = PC::NotAllowed;

        add({"freezeattempts", "Freeze Attempts", "The attempt counter doesn't go up.", Level, OK});
        add({"instantcomplete", "Instant Complete", "Completes the level right after it starts.", Level, NO});
        add({"nocamerashake", "No Camera Shake", "Turns off all camera shake.", Level, OK});
        add({"pausecountdown", "Pause Countdown", "Counts down before the game continues after unpausing.", Level, OK,
            false, [] { optInt("Seconds", "pausecountdown.seconds", 3, 1, 10); }});
        add({"classicpercentage", "Classic Percentage", "% by how far along the level you are (like before 2.2), not by time.", Level, OK});
        add({"bestinpercentage", "Best in Percentage", "Shows your best next to the current %, e.g. 43% / 61%.", Level, OK});
        add({"cointracers", "Coin Tracers", "Draws lines from you to every coin you haven't collected.", Level, OK});
        add({"norotation", "No Rotation", "Your icon doesn't rotate.", Player, OK});
        add({"nowavetrail", "No Wave Trail", "Hides the wave trail.", Player, OK});
        add({"notrailblending", "No Trail Blending", "Draws the trail without the glowing (additive) look.", Player, OK});
        add({"noghosttrail", "No Ghost Trail", "Ignores ghost trail triggers.", Player, OK});
        add({"forceghosttrail", "Force Ghost Trail", "Ghost trail always on.", Player, OK});
        add({"forceshowplayer", "Force Show Player", "Ignores triggers that hide the player.", Player, SOMETIMES});

        Hack suicide{"suicide", "Kill Player", "Kills the player right away.", Shortcuts, OK};
        suicide.action = [] {
            auto pl = PlayLayer::get();
            if (!pl || pl->m_playerDied) return;
            stats::forceKill = true;
            pl->destroyPlayer(pl->m_player1, nullptr);
            stats::forceKill = false;
        };
        add(std::move(suicide));

        Hack options{"opensettings", "Open Settings", "Opens GD's settings from anywhere.", Shortcuts, OK};
        options.action = [] {
            if (auto layer = OptionsLayer::create()) {
                CCDirector::get()->getRunningScene()->addChild(layer, 1000);
                layer->showLayer(false);
            }
        };
        add(std::move(options));

        Hack resetMusic{"resetmusic", "Reset Music Volume", "Sets the music volume back to 100%.", Shortcuts, OK};
        resetMusic.action = [] {
            FMODAudioEngine::sharedEngine()->setBackgroundMusicVolume(1.f);
            GameManager::get()->m_bgVolume = 1.f;
        };
        add(std::move(resetMusic));

        Hack resetSfx{"resetsfx", "Reset SFX Volume", "Sets the sound effects volume back to 100%.", Shortcuts, OK};
        resetSfx.action = [] {
            FMODAudioEngine::sharedEngine()->setEffectsVolume(1.f);
            GameManager::get()->m_sfxVolume = 1.f;
        };
        add(std::move(resetSfx));
    }
}
