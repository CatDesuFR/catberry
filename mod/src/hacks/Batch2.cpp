// Second batch of modules from QOLMod / Eclipse's feature lists, written from scratch.
#include <core/Hacks.hpp>
#include <core/Log.hpp>
#include <features/Sim.hpp>
#include <features/WorldOverlay.hpp>
#include <Geode/fmod/fmod.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Pitch Shift: an FMOD pitch-shift effect on the music (or all audio), so the
// pitch changes without changing the speed.
// ---------------------------------------------------------------------------
namespace {
    FMOD::DSP* s_pitchDsp = nullptr;
    FMOD::ChannelGroup* s_pitchGroup = nullptr;
    float s_pitchTimer = 0.f;

    void updatePitch() {
        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_system) return;
        bool on = cmm::on("pitchshift");
        auto group = cmm::val<bool>("pitchshift.musiconly", true) ? engine->m_backgroundMusicChannel : engine->m_globalChannel;

        // Remove it from the old group when turned off or the target changed.
        if (s_pitchDsp && s_pitchGroup && (!on || s_pitchGroup != group)) {
            s_pitchGroup->removeDSP(s_pitchDsp);
            s_pitchGroup = nullptr;
        }
        if (!on || !group) return;
        if (!s_pitchDsp && engine->m_system->createDSPByType(FMOD_DSP_TYPE_PITCHSHIFT, &s_pitchDsp) != FMOD_OK) {
            s_pitchDsp = nullptr;
            return;
        }
        if (!s_pitchGroup) {
            group->addDSP(0, s_pitchDsp);
            s_pitchGroup = group;
        }
        s_pitchDsp->setParameterFloat(FMOD_DSP_PITCHSHIFT_PITCH, std::clamp(cmm::val<float>("pitchshift.value", 1.f), 0.5f, 2.f));
    }
}

class $modify(B2Scheduler, CCScheduler) {
    void update(float dt) {
        CCScheduler::update(dt);
        s_pitchTimer -= dt;
        if (s_pitchTimer > 0.f) return;
        s_pitchTimer = 0.25f;
        updatePitch();
    }
};

// ---------------------------------------------------------------------------
// Level / player modules
// ---------------------------------------------------------------------------
class $modify(B2PlayLayer, PlayLayer) {
    struct Fields {
        Ref<CCDrawNode> trajectory;
        bool layoutApplied = false;
    };

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        auto node = CCDrawNode::create();
        node->setID("cats-mod-menu-trajectory");
        this->addChild(node, 996);
        m_fields->trajectory = node;
        if (cmm::on("layoutmode")) applyLayout(true);
    }

    // Layout Mode: hide decoration so only gameplay objects are left.
    void applyLayout(bool on) {
        if (!m_objects) return;
        for (auto o : CCArrayExt<GameObject*>(m_objects)) {
            if (o && o->m_objectType == GameObjectType::Decoration) o->m_isHide = on;
        }
        m_fields->layoutApplied = on;
    }

    // Show Trajectory: where you'd go if you held from now on, and if you let go.
    void drawTrajectory() {
        auto f = m_fields.self();
        if (!f->trajectory) return;
        f->trajectory->clear();
        if (!cmm::on("trajectory") || m_playerDied || !m_player1) return;
        cmm::overlay::syncTo(f->trajectory, m_objectLayer, this);

        int length = std::clamp(cmm::val<int>("trajectory.length", 240), 10, 1000);
        auto p = cmm::sim::scratch(this, 1);
        if (!p) return;
        bool dual = m_player2 && m_gameState.m_isDualMode;
        for (int run = 0; run < (dual ? 4 : 2); run++) {
            int hold = run % 2;
            auto source = run < 2 ? m_player1 : m_player2;
            cmm::sim::copyState(p, source);
            cmm::sim::clearStopped();
            if (hold) p->pushButton(PlayerButton::Jump);
            else p->releaseButton(PlayerButton::Jump);
            ccColor4F col = hold ? ccColor4F{0.3f, 1.f, 0.4f, 0.9f} : ccColor4F{1.f, 0.35f, 0.35f, 0.9f};
            CCPoint last = p->getPosition();
            for (int k = 0; k < length; k++) {
                bool alive = cmm::sim::step(this, p);
                auto now = p->getPosition();
                f->trajectory->drawSegment(last, now, 0.5f, col);
                last = now;
                if (!alive) {
                    // Mark where that path dies.
                    if (!cmm::sim::stopped()) f->trajectory->drawDot(now, 3.f, {1.f, 1.f, 1.f, 1.f});
                    break;
                }
            }
            cmm::sim::restoreTouched();
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto f = m_fields.self();

        bool layout = cmm::on("layoutmode");
        if (layout != f->layoutApplied) applyLayout(layout);

        for (auto pl : {m_player1, m_player2}) {
            if (!pl) continue;
            if (cmm::on("nodashfire") && pl->m_dashFireSprite) pl->m_dashFireSprite->setVisible(false);
            if (cmm::on("noswingfire")) {
                for (auto fire : {pl->m_swingFireMiddle, pl->m_swingFireBottom, pl->m_swingFireTop}) {
                    if (fire) fire->setVisible(false);
                }
            }
            if (auto wave = pl->m_waveTrail) {
                if (cmm::on("nowavepulse")) wave->m_pulseSize = 1.f;
                if (cmm::on("wavetrailsize")) wave->m_waveSize = std::clamp(cmm::val<float>("wavetrailsize.value", 1.f), 0.1f, 5.f);
            }
        }

        drawTrajectory();
    }

    // Hide Percentage on Complete
    void levelComplete() {
        PlayLayer::levelComplete();
        if (!cmm::on("hidepercentcomplete")) return;
        for (CCNode* node : {static_cast<CCNode*>(m_percentageLabel), static_cast<CCNode*>(m_progressBar)}) {
            if (node) node->runAction(CCFadeOut::create(0.5f));
        }
    }
};

// Menu Snow
class $modify(B2Menu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        if (cmm::on("menusnow")) {
            if (auto snow = CCParticleSnow::create()) {
                snow->setPosition({CCDirector::get()->getWinSize().width / 2, CCDirector::get()->getWinSize().height + 10});
                snow->setID("cats-mod-menu-snow");
                this->addChild(snow, 100);
            }
        }
        return true;
    }
};

namespace cmm {
    void registerBatch2() {
        using enum Tab;
        constexpr auto OK = PC::Allowed;
        constexpr auto NO = PC::NotAllowed;

        add({"trajectory", "Show Trajectory", "Draws where you'd go if you held (green) or let go (red) from now on. A white dot marks a death.", Level, NO,
            false, [] { optInt("Length (ticks)", "trajectory.length", 240, 10, 1000); }});
        add({"layoutmode", "Layout Mode", "Hides decoration so only gameplay objects are left.", Level, NO});
        add({"hidepercentcomplete", "Hide % on Complete", "Fades the % and progress bar out when you finish a level.", Level, OK});
        add({"pitchshift", "Pitch Shift", "Changes the pitch of the audio without changing its speed.", Global, OK,
            false, [] {
                optFloat("Pitch", "pitchshift.value", 1.f, 0.5f, 2.f, "%.2fx");
                optBool("Music only", "pitchshift.musiconly", true);
            }});
        add({"menusnow", "Menu Snow", "Snow falling on the main menu (shows next time you open it).", Global, OK});
        add({"nodashfire", "No Dash Fire", "Hides the fire when you use a dash orb.", Player, OK});
        add({"noswingfire", "No Swing Fire", "Hides the swing's boost fire.", Player, OK});
        add({"nowavepulse", "No Wave Pulse", "The wave trail doesn't pulse to the music.", Player, OK});
        add({"wavetrailsize", "Wave Trail Size", "Thicker or thinner wave trail.", Player, OK,
            false, [] { optFloat("Size", "wavetrailsize.value", 1.f, 0.1f, 5.f, "%.2fx"); }});
    }
}
