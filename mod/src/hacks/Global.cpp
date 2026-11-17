#include <core/Hacks.hpp>
#include <core/Indicator.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/CCTransitionFade.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/fmod/fmod.hpp>
#include <cmath>

using namespace geode::prelude;

namespace {
    // The music (and GD's other sound) at `pitch` times its normal speed.
    // Checked every frame: a level brings new channels, which start at 1x.
    void setGlobalPitch(float pitch) {
        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine) return;
        // A group under another one already gets its speed from it (setting
        // both would apply the speed twice).
        auto under = [](FMOD::ChannelGroup* group, FMOD::ChannelGroup* ancestor) {
            for (int depth = 0; group && depth < 8; depth++) {
                FMOD::ChannelGroup* parent = nullptr;
                if (group->getParentGroup(&parent) != FMOD_OK || !parent) return false;
                if (parent == ancestor) return true;
                group = parent;
            }
            return false;
        };
        auto music = engine->m_backgroundMusicChannel, global = engine->m_globalChannel;
        if (music && global && under(music, global)) music = nullptr;
        for (FMOD::ChannelGroup* group : {music, global}) {
            if (!group) continue;
            float now = 1.f;
            group->getPitch(&now);
            if (std::abs(now - pitch) > 0.0001f) group->setPitch(pitch);
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
