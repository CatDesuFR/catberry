#include <core/Hacks.hpp>
#include <core/Indicator.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/CCTransitionFade.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/fmod/fmod.hpp>

using namespace geode::prelude;

namespace {
    float s_lastPitch = 1.f;

    void setGlobalPitch(float pitch) {
        if (pitch == s_lastPitch) return;
        auto engine = FMODAudioEngine::sharedEngine();
        if (engine && engine->m_globalChannel) {
            engine->m_globalChannel->setPitch(pitch);
            s_lastPitch = pitch;
        }
    }
}

class $modify(CMMScheduler, CCScheduler) {
    void update(float dt) {
        float speed = 1.f;
        if (cmm::on("speedhack")) speed = std::max(0.01f, cmm::val<float>("speedhack.value", 1.f));

        bool syncAudio = cmm::on("speedhack") && cmm::val<bool>("speedhack.audio", true);
        float pitch = syncAudio ? speed : 1.f;
        if (cmm::on("audiospeed")) pitch = std::max(0.01f, cmm::val<float>("audiospeed.value", 1.f));
        setGlobalPitch(pitch);

        CCScheduler::update(dt * speed);
    }
};

class $modify(CMMTransition, CCTransitionFade) {
    static CCTransitionFade* create(float duration, CCScene* scene) {
        if (cmm::on("transitionspeed")) duration *= cmm::val<float>("transitionspeed.value", 0.f);
        return CCTransitionFade::create(duration, scene);
    }
};

// Safe Mode: never save progress. Auto Safe Mode: only while a
// not-allowed hack is (or was) on this attempt.
class $modify(CMMSafeMode, PlayLayer) {
    bool shouldBeSafe() {
        if (cmm::on("safemode")) return true;
        return cmm::on("autosafemode") && cmm::indicator::usedThisAttempt();
    }

    void levelComplete() {
        bool safe = shouldBeSafe();
        bool old = m_isTestMode;
        if (safe) m_isTestMode = true;
        PlayLayer::levelComplete();
        if (safe) m_isTestMode = old;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool safe = shouldBeSafe() && object != m_anticheatSpike;
        bool old = m_isTestMode;
        if (safe) m_isTestMode = true;
        PlayLayer::destroyPlayer(player, object);
        if (safe) m_isTestMode = old;
    }
};

namespace cmm::global {
    void applyFps() {
        double fps = cmm::on("fpsbypass") ? cmm::val<float>("fpsbypass.value", 240.f) : 0.0;
        if (fps <= 0.0) return;
        CCApplication::sharedApplication()->setAnimationInterval(1.0 / fps);
    }
}
