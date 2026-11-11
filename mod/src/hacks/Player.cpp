#include <core/Hacks.hpp>
#include <features/Stats.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/fmod/fmod.hpp>

using namespace geode::prelude;

namespace cmm::visual { void noclipFlash(); }

class $modify(CMMNoclip, PlayLayer) {
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (object != m_anticheatSpike && !cmm::stats::forceKill && cmm::on("noclip")) {
            bool p1 = cmm::val<bool>("noclip.p1", true);
            bool p2 = cmm::val<bool>("noclip.p2", true);
            bool isP2 = player == m_player2;
            // Optional limits: let the death through once you're past them.
            int maxDeaths = cmm::val<int>("noclip.maxdeaths", 0);
            float minAcc = cmm::val<float>("noclip.minacc", 0.f);
            bool overLimit = (maxDeaths > 0 && cmm::stats::noclipDeaths >= maxDeaths && !cmm::stats::noclipHitLastFrame)
                || (minAcc > 0.f && cmm::stats::noclipAccuracy() < minAcc);
            if (!overLimit && ((isP2 && p2) || (!isP2 && p1))) {
                cmm::stats::noclipHitThisFrame = true;
                cmm::visual::noclipFlash();
                return;
            }
        }

        bool wasDead = m_playerDied;
        PlayLayer::destroyPlayer(player, object);

        if (!wasDead && m_playerDied && object != m_anticheatSpike && cmm::on("mutelevelaudioondeath")) {
            auto engine = FMODAudioEngine::sharedEngine();
            if (engine && engine->m_backgroundMusicChannel) engine->m_backgroundMusicChannel->setPaused(true);
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        auto engine = FMODAudioEngine::sharedEngine();
        if (engine && engine->m_backgroundMusicChannel) engine->m_backgroundMusicChannel->setPaused(false);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        static bool hidden = false;
        bool hide = cmm::on("hideplayer");
        if (hide || hidden) {
            for (auto p : {m_player1, m_player2}) {
                if (p) p->setVisible(!hide);
            }
            hidden = hide;
        }
    }
};

class $modify(CMMPlayer, PlayerObject) {
    void playDeathEffect() {
        if (cmm::on("nodeatheffect")) return;
        PlayerObject::playDeathEffect();
    }

    void playSpawnEffect() {
        if (cmm::on("norespawnflash")) return;
        PlayerObject::playSpawnEffect();
    }

    void update(float dt) {
        PlayerObject::update(dt);

        if (m_regularTrail) {
            if (cmm::on("notrail")) m_regularTrail->setVisible(false);
            else if (cmm::on("alwaystrail")) m_regularTrail->setVisible(true);
        }
        if (m_waveTrail && cmm::on("solidwavetrail")) {
            m_waveTrail->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
        }
        if (m_robotFire && cmm::on("norobotfire")) m_robotFire->setVisible(false);
        if (cmm::on("novehicleparticles")) {
            if (m_vehicleGroundParticles) m_vehicleGroundParticles->setVisible(false);
            if (m_playerGroundParticles) m_playerGroundParticles->setVisible(false);
        }
    }
};

class $modify(CMMPlayerInput, GJBaseGameLayer) {
    struct Fields {
        int tick = 0;
        bool autoDown = false;
    };

    void handleButton(bool down, int button, bool isPlayer1) {
        if (down && button == 1 && cmm::on("jumphack")) {
            auto p = isPlayer1 ? m_player1 : m_player2;
            if (p) p->m_isOnGround = true;
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto pl = PlayLayer::get();
        if (cmm::on("autoclicker") && pl && static_cast<GJBaseGameLayer*>(pl) == this && !m_playerDied) {
            auto f = m_fields.self();
            int hold = std::max(1, cmm::val<int>("autoclicker.hold", 2));
            int release = std::max(1, cmm::val<int>("autoclicker.release", 2));
            f->tick++;
            if (f->autoDown && f->tick >= hold) {
                this->handleButton(false, 1, true);
                f->autoDown = false;
                f->tick = 0;
            } else if (!f->autoDown && f->tick >= release) {
                this->handleButton(true, 1, true);
                f->autoDown = true;
                f->tick = 0;
            }
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
};
