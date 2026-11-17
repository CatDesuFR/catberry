// Third batch of modules from QOLMod / Eclipse's feature lists, written from scratch.
#include <core/Hacks.hpp>
#include <algorithm>
#include <core/Log.hpp>
#include <Geode/modify/AudioEffectsLayer.hpp>
#include <Geode/modify/CCCircleWave.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <imgui.h>

using namespace geode::prelude;

namespace {
    bool isPlayLayer(GJBaseGameLayer* layer) {
        auto pl = PlayLayer::get();
        return pl && static_cast<GJBaseGameLayer*>(pl) == layer;
    }

    struct ParticleOption {
        char const* key;
        char const* label;
    };
    constexpr ParticleOption kParticles[] = {
        {"particles.ground", "Ground particles"},
        {"particles.trailing", "Trailing particles"},
        {"particles.shipclick", "Ship click particles"},
        {"particles.vehicleground", "Vehicle ground particles"},
        {"particles.ufoclick", "UFO click particles"},
        {"particles.robotburst", "Robot burst particles"},
    };
}

// No Circles: the circle effects from orbs, pads and portals.
class $modify(B3Circle, CCCircleWave) {
    static CCCircleWave* create(float startRadius, float endRadius, float duration, bool fadeIn, bool easeOut) {
        auto wave = CCCircleWave::create(startRadius, endRadius, duration, fadeIn, easeOut);
        if (wave && cmm::on("nocircles") && PlayLayer::get()) wave->setVisible(false);
        return wave;
    }
};

class $modify(B3GameLayer, GJBaseGameLayer) {
    // All Passable: the player goes through solid blocks and slopes. GD checks
    // hazards in the same pass, so only the solids are taken out of the list.
    void collisionCheckObjects(PlayerObject* player, std::vector<GameObject*>* objects, int count, float dt) {
        if (!cmm::on("allpassable") || !isPlayLayer(this) || !objects) {
            return GJBaseGameLayer::collisionCheckObjects(player, objects, count, dt);
        }
        std::vector<GameObject*> kept;
        kept.reserve(static_cast<size_t>(std::max(0, count)));
        for (int i = 0; i < count && i < static_cast<int>(objects->size()); i++) {
            auto o = (*objects)[static_cast<size_t>(i)];
            if (o && (o->m_objectType == GameObjectType::Solid || o->m_objectType == GameObjectType::Slope)) continue;
            kept.push_back(o);
        }
        GJBaseGameLayer::collisionCheckObjects(player, &kept, static_cast<int>(kept.size()), dt);
    }

    // No Particle Triggers
    void spawnParticleTrigger(int particleID, CCPoint position, float rotation, float scale) {
        if (cmm::on("noparticletriggers") && isPlayLayer(this)) return;
        GJBaseGameLayer::spawnParticleTrigger(particleID, position, rotation, scale);
    }
};

class $modify(B3PlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (cmm::on("hideattempts") && m_attemptLabel) m_attemptLabel->setVisible(false);

        if (!cmm::on("particletoggles")) return;
        for (auto p : {m_player1, m_player2}) {
            if (!p) continue;
            CCParticleSystemQuad* systems[] = {
                p->m_playerGroundParticles, p->m_trailingParticles, p->m_shipClickParticles,
                p->m_vehicleGroundParticles, p->m_ufoClickParticles, p->m_robotBurstParticles,
            };
            for (size_t i = 0; i < std::size(systems); i++) {
                if (systems[i] && cmm::val<bool>(kParticles[i].key, false)) systems[i]->setVisible(false);
            }
        }
    }
};

// No Music Fade Out
class $modify(B3Audio, FMODAudioEngine) {
    void fadeOutMusic(float duration, int channel) {
        if (cmm::on("nomusicfadeout") && PlayLayer::get()) return;
        FMODAudioEngine::fadeOutMusic(duration, channel);
    }
};

// Auto Song Download
class $modify(B3LevelInfo, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        if (cmm::on("autosongdownload") && m_songWidget && m_songWidget->m_downloadBtn && m_songWidget->m_downloadBtn->isVisible()) {
            m_songWidget->onDownload(nullptr);
            cmm::log::info("Auto Song Download: downloading the song for {}", std::string(level->m_levelName));
        }
        return true;
    }
};

// Hide Editor UI
class $modify(B3Scheduler, CCScheduler) {
    void update(float dt) {
        CCScheduler::update(dt);
        static bool hidden = false;
        auto editor = LevelEditorLayer::get();
        bool want = cmm::on("hideeditorui") && editor && editor->m_editorUI;
        if (editor && editor->m_editorUI && want != hidden) {
            editor->m_editorUI->setVisible(!want);
        }
        hidden = want && editor;
    }
};

// No Orb Pulse: orbs, pads and other music-scaled objects stop pulsing.
// GD sizes them from the audio effects layer's m_audioScale (0.4 quiet to 1.2
// loud), worked out from FMOD's pulse values. Right after a restart GD sizes
// them before that layer has stepped, straight from the pulse values, so both
// are held at the chosen size: the layer's value after every audio step, and
// FMOD's pulses while GD works out what's on screen. (The song isn't touched.)
namespace {
    float orbSize() { return std::clamp(cmm::val<float>("noorbpulse.size", 0.75f), 0.f, 1.f); }
}

class $modify(B3OrbPulseAudio, AudioEffectsLayer) {
    void audioStep(float dt) {
        AudioEffectsLayer::audioStep(dt);
        if (cmm::on("noorbpulse") && PlayLayer::get()) m_audioScale = orbSize();
    }
};

class $modify(B3OrbPulse, PlayLayer) {
    void updateVisibility(float dt) {
        if (!cmm::on("noorbpulse")) return PlayLayer::updateVisibility(dt);
        float v = orbSize();
        auto fmod = FMODAudioEngine::sharedEngine();
        float p1 = fmod->m_pulse1, p2 = fmod->m_pulse2, p3 = fmod->m_pulse3;
        fmod->m_pulse1 = fmod->m_pulse2 = fmod->m_pulse3 = v;
        float old = m_audioEffectsLayer ? m_audioEffectsLayer->m_audioScale : 0.f;
        if (m_audioEffectsLayer) m_audioEffectsLayer->m_audioScale = v;
        PlayLayer::updateVisibility(dt);
        if (m_audioEffectsLayer) m_audioEffectsLayer->m_audioScale = old;
        fmod->m_pulse1 = p1;
        fmod->m_pulse2 = p2;
        fmod->m_pulse3 = p3;
    }
};

// No Pad Signal: no flash on a pad or orb when you use it.
class $modify(B3PadSignal, GameObject) {
    void playShineEffect() {
        if (cmm::on("nopadsignal") && PlayLayer::get()) {
            switch (m_objectType) {
                case GameObjectType::YellowJumpPad: case GameObjectType::PinkJumpPad: case GameObjectType::GravityPad:
                case GameObjectType::RedJumpPad: case GameObjectType::SpiderPad:
                case GameObjectType::YellowJumpRing: case GameObjectType::PinkJumpRing: case GameObjectType::GravityRing:
                case GameObjectType::GreenRing: case GameObjectType::DropRing: case GameObjectType::RedJumpRing:
                case GameObjectType::CustomRing: case GameObjectType::DashRing: case GameObjectType::GravityDashRing:
                case GameObjectType::SpiderOrb: case GameObjectType::TeleportOrb:
                    return;
                default: break;
            }
        }
        GameObject::playShineEffect();
    }
};

// No Pad Signal / No Gravity Signal: GD tells the level's Event triggers when
// you touch or use an orb, use a pad, or flip gravity; levels use those
// "signals" for effects (like making orbs pulse or the screen flash). These
// stop the signals, so those effects don't run.
class $modify(B3GameEvents, GJBaseGameLayer) {
    void gameEventTriggered(GJGameEvent event, int material, int playerID) {
        if (PlayLayer::get() && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this) {
            bool pad = event == GJGameEvent::OrbTouched || event == GJGameEvent::OrbActivated || event == GJGameEvent::PadActivated;
            bool gravity = event == GJGameEvent::GravityInverted || event == GJGameEvent::GravityRestored;
            if ((pad && cmm::on("nopadsignal")) || (gravity && cmm::on("nogravitysignal"))) return;
        }
        GJBaseGameLayer::gameEventTriggered(event, material, playerID);
    }
};

// No Gravity Signal also hides GD's arrows on screen when gravity flips.
class $modify(B3GravitySignal, PlayLayer) {
    void playGravityEffect(bool flip) {
        if (cmm::on("nogravitysignal")) return;
        PlayLayer::playGravityEffect(flip);
    }
};

namespace cmm {
    void registerBatch3() {
        using enum Tab;
        constexpr auto OK = PC::Allowed;
        constexpr auto NO = PC::NotAllowed;

        add({"nocircles", "No Circles", "Hides the circle effects from orbs, pads and portals.", Level, OK});
        add({"nopadsignal", "No Pad Signal", "Touching or using an orb, or using a pad, no longer sets off the level's effects for it (like orbs pulsing or the screen flashing), and pads and orbs don't flash.", Level, OK});
        add({"nogravitysignal", "No Gravity Signal", "Flipping gravity no longer sets off the level's effects for it, and GD's arrows don't show on screen.", Level, OK});
        add({"noorbpulse", "No Orb Pulse", "Orbs and pads stop pulsing to the music and stay one size.", Level, OK,
            false, [] { optFloat("Orb size (0.75 = normal size)", "noorbpulse.size", 0.75f, 0.f, 1.f, "%.2f"); }});
        add({"particletoggles", "Player Particles", "Hide each kind of player particle separately (see options).", Player, OK,
            false, [] {
                for (auto const& p : kParticles) optBool(p.label, p.key, false);
                ImGui::TextDisabled("Ticked = hidden");
            }});
        add({"noparticletriggers", "No Particle Triggers", "Particle objects from the level's triggers don't spawn.", Level, OK});
        add({"allpassable", "All Passable", "You pass through solid blocks.", Level, NO});
        add({"hideattempts", "Hide Attempt Label", "Hides the \"Attempt N\" text in levels.", Level, OK});
        add({"nomusicfadeout", "No Music Fade Out", "The song doesn't fade out at the end of a level.", Level, OK});
        add({"autosongdownload", "Auto Song Download", "Downloads a level's song when you open the level page.", Bypass, OK});
        add({"hideeditorui", "Hide Editor UI", "Hides the editor's buttons and toolbars (bind it to a key to toggle).", Creator, OK});
    }
}
