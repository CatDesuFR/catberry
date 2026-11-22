#include "Sim.hpp"
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/EnhancedGameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <unordered_set>

using namespace geode::prelude;

namespace cmm::sim {
    namespace {
        bool s_simulating = false;
        bool s_died = false;
        bool s_stopped = false;
        PlayerObject* s_player = nullptr;
        float s_stepDt = 0.25f;

        struct Touched {
            Ref<EnhancedGameObject> obj;
            bool p1, p2, active;
        };
        std::vector<Touched> s_touched;
        std::unordered_set<EnhancedGameObject*> s_touchedSet;

        PlayLayer* s_owner = nullptr;
        // Slots: 0-2 CBF/trajectory/replay analysis, 3 own-click tests,
        // 4-11 clicks waiting to be measured, 12+ the recent-ticks history.
        Ref<PlayerObject> s_scratch[192];
        Ref<PlayerCheckpoint> s_checkpoint;

        void hide(PlayerObject* p) {
            p->setVisible(false);
            if (p->m_regularTrail) p->m_regularTrail->setVisible(false);
            if (p->m_waveTrail) p->m_waveTrail->setVisible(false);
            if (p->m_shipStreak) p->m_shipStreak->setVisible(false);
        }
    }

    PlayerObject* scratch(PlayLayer* pl, int slot) {
        if (s_owner != pl) {
            for (auto& p : s_scratch) p = nullptr;
            s_owner = pl;
        }
        if (slot < 0 || slot >= static_cast<int>(std::size(s_scratch))) return nullptr;
        auto& p = s_scratch[slot];
        if (!p) {
            p = PlayerObject::create(1, 1, pl, pl->m_objectLayer, false);
            if (!p) return nullptr;
            pl->m_objectLayer->addChild(p, -100);
            hide(p);
        }
        return p;
    }

    void copyState(PlayerObject* dst, PlayerObject* src) {
        if (!s_checkpoint) {
            auto cp = new PlayerCheckpoint();
            cp->init();
            cp->autorelease();
            s_checkpoint = cp;
        }
        bool was = s_simulating;
        s_simulating = true;  // block anything loading the state might trigger
        s_player = dst;
        dst->copyAttributes(src);
        // GD's own practice checkpoint holds the physics state it needs to
        // resume a player exactly; load that on top.
        src->saveToCheckpoint(s_checkpoint);
        dst->loadFromCheckpoint(s_checkpoint);
        dst->m_yVelocity = src->m_yVelocity;
        dst->m_isOnGround = src->m_isOnGround;
        dst->m_isOnGround2 = src->m_isOnGround2;
        dst->m_isOnGround3 = src->m_isOnGround3;
        dst->m_isOnGround4 = src->m_isOnGround4;
        dst->m_isOnSlope = src->m_isOnSlope;
        dst->m_wasOnSlope = src->m_wasOnSlope;
        dst->m_currentSlope = src->m_currentSlope;
        dst->m_currentSlope2 = src->m_currentSlope2;
        dst->m_currentSlopeYVelocity = src->m_currentSlopeYVelocity;
        dst->m_collidedObject = src->m_collidedObject;
        dst->m_holdingButtons = src->m_holdingButtons;
        dst->m_jumpBuffered = src->m_jumpBuffered;
        dst->m_stateJumpBuffered = src->m_stateJumpBuffered;
        dst->m_touchedRings = src->m_touchedRings;
        dst->m_touchedRing = src->m_touchedRing;
        dst->m_ringJumpedSincePress = src->m_ringJumpedSincePress;
        dst->m_lastJumpTime = src->m_lastJumpTime;
        dst->m_lastFlipTime = src->m_lastFlipTime;
        dst->m_speedMultiplier = src->m_speedMultiplier;
        dst->m_playerSpeed = src->m_playerSpeed;
        dst->m_isPlatformer = src->m_isPlatformer;
        dst->m_fixGravityBug = src->m_fixGravityBug;
        dst->m_isLocked = src->m_isLocked;
        dst->m_controlsDisabled = src->m_controlsDisabled;
        dst->m_isGoingLeft = src->m_isGoingLeft;
        dst->m_reverseRelated = src->m_reverseRelated;
        dst->m_position = src->m_position;
        dst->m_isDead = false;
        dst->setRotation(src->getRotation());
        dst->setPosition(src->getPosition());
        hide(dst);
        s_simulating = was;
    }

    bool step(PlayLayer* pl, PlayerObject* p) { return stepPart(pl, p, 1.f); }

    bool stepPart(PlayLayer* pl, PlayerObject* p, float fraction) {
        float dt = s_stepDt * fraction;
        s_simulating = true;
        s_player = p;
        s_died = false;
        p->update(dt);
        pl->checkCollisions(p, dt, false);
        p->updateRotation(dt);
        s_simulating = false;
        s_player = nullptr;
        return !s_died && !s_stopped && !p->m_isDead;
    }

    void restoreTouched() {
        for (auto& t : s_touched) {
            t.obj->m_activatedByPlayer1 = t.p1;
            t.obj->m_activatedByPlayer2 = t.p2;
            t.obj->m_isActivated = t.active;
        }
        s_touched.clear();
        s_touchedSet.clear();
    }

    bool isScratch(CCObject* obj) {
        for (auto& p : s_scratch) {
            if (p && p.data() == obj) return true;
        }
        return false;
    }

    bool simulating() { return s_simulating; }
    bool stopped() { return s_stopped; }
    void clearStopped() { s_stopped = false; }
    float stepDt() { return s_stepDt; }
}

using namespace cmm::sim;

class $modify(SimPlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayLayer::destroyPlayer", Priority::First);
        (void)self.setHookPriority("PlayLayer::levelComplete", Priority::First);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (s_simulating) {
            if (player == s_player) s_died = true;
            return;
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        if (s_simulating) { s_stopped = true; return; }
        PlayLayer::levelComplete();
    }

    void onQuit() {
        restoreTouched();
        for (auto& p : s_scratch) p = nullptr;
        s_owner = nullptr;
        PlayLayer::onQuit();
    }
};

class $modify(SimGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("GJBaseGameLayer::playerTouchedTrigger", Priority::First);
        (void)self.setHookPriority("GJBaseGameLayer::pickupItem", Priority::First);
        (void)self.setHookPriority("GJBaseGameLayer::destroyObject", Priority::First);
        (void)self.setHookPriority("GJBaseGameLayer::gameEventTriggered", Priority::First);
        (void)self.setHookPriority("GJBaseGameLayer::toggleDualMode", Priority::First);
        (void)self.setHookPriority("GJBaseGameLayer::playerWillSwitchMode", Priority::First);
        (void)self.setHookPriority("GJBaseGameLayer::toggleFlipped", Priority::First);
        (void)self.setHookPriority("GJBaseGameLayer::shakeCamera", Priority::First);
        (void)self.setHookPriority("GJBaseGameLayer::lightningFlash", Priority::First);
    }

    void playerTouchedTrigger(PlayerObject* p, EffectGameObject* o) { if (s_simulating) return; GJBaseGameLayer::playerTouchedTrigger(p, o); }
    void pickupItem(EffectGameObject* o) { if (s_simulating) return; GJBaseGameLayer::pickupItem(o); }
    void destroyObject(GameObject* o) { if (s_simulating) return; GJBaseGameLayer::destroyObject(o); }
    void gameEventTriggered(GJGameEvent e, int m, int id) { if (s_simulating) return; GJBaseGameLayer::gameEventTriggered(e, m, id); }
    void toggleDualMode(GameObject* o, bool dual, PlayerObject* p, bool noEffects) {
        if (s_simulating) { s_stopped = true; return; }
        GJBaseGameLayer::toggleDualMode(o, dual, p, noEffects);
    }
    void playerWillSwitchMode(PlayerObject* p, GameObject* o) {
        if (s_simulating) { s_stopped = true; return; }
        GJBaseGameLayer::playerWillSwitchMode(p, o);
    }
    void toggleFlipped(bool flip, bool noEffects) { if (s_simulating) return; GJBaseGameLayer::toggleFlipped(flip, noEffects); }
    void shakeCamera(float d, float s, float i) { if (s_simulating) return; GJBaseGameLayer::shakeCamera(d, s, i); }
    void lightningFlash(CCPoint from, CCPoint to, ccColor3B c, float lw, float d, int disp, bool flash, float op) {
        if (s_simulating) return;
        GJBaseGameLayer::lightningFlash(from, to, c, lw, d, disp, flash, op);
    }
};

class $modify(SimPlayer, PlayerObject) {
    // Use exactly the dt GD gives the real player each tick.
    void update(float dt) {
        if (!s_simulating) {
            auto pl = PlayLayer::get();
            if (pl && this == pl->m_player1) s_stepDt = dt;
        }
        PlayerObject::update(dt);
    }

    void playDeathEffect() { if (s_simulating) return; PlayerObject::playDeathEffect(); }
    void playSpawnEffect() { if (s_simulating) return; PlayerObject::playSpawnEffect(); }
};

class $modify(SimEffect, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int uniqueID, std::vector<int> const* remapKeys) {
        if (s_simulating) return;
        EffectGameObject::triggerObject(layer, uniqueID, remapKeys);
    }
};

class $modify(SimEnhanced, EnhancedGameObject) {
    // GD asks this before marking an orb/pad as used, so remember the old
    // flags and put them back after the run.
    bool hasBeenActivatedByPlayer(PlayerObject* player) {
        if (s_simulating && s_touchedSet.insert(this).second) {
            s_touched.push_back({this, m_activatedByPlayer1, m_activatedByPlayer2, m_isActivated});
        }
        return EnhancedGameObject::hasBeenActivatedByPlayer(player);
    }
};
