#include <core/Hacks.hpp>
#include <features/WorldOverlay.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

namespace cmm {
    // Colour for Best Color: preset or custom hex.
    ccColor3B bestColor() {
        switch (val<int>("bestcolor.preset", 0)) {
            case 0: return {255, 200, 40};   // golden
            case 1: return {200, 205, 220};  // silver
            case 2: return {175, 90, 255};   // purple
            default: {
                auto hex = val<std::string>("bestcolor.hex", "FF4FA3");
                unsigned v = 0xFFFFFF;
                try { v = std::stoul(hex, nullptr, 16); } catch (...) {}
                return {static_cast<GLubyte>(v >> 16), static_cast<GLubyte>(v >> 8), static_cast<GLubyte>(v)};
            }
        }
    }
}

class $modify(CMMLevel, PlayLayer) {
    struct Fields {
        bool drawingHitboxes = false;
        // GD's hitbox node sits in a layer that 2.2 doesn't draw, so we draw
        // its contents through this proxy on the PlayLayer.
        cmm::overlay::Proxy* proxy = nullptr;
        cmm::overlay::Proxy* proxy2 = nullptr;  // GD's second hitbox node
    };

    void showProxy(bool show) {
        auto f = m_fields.self();
        if (!f->proxy && show) {
            f->proxy = cmm::overlay::Proxy::create();
            f->proxy->setID("cats-mod-menu-hitboxes");
            this->addChild(f->proxy, 1000);
        }
        if (!f->proxy2 && show) {
            f->proxy2 = cmm::overlay::Proxy::create();
            f->proxy2->setID("cats-mod-menu-hitboxes-2");
            this->addChild(f->proxy2, 999);
        }
        if (!f->proxy) return;
        f->proxy->setVisible(show && m_debugDrawNode);
        if (show && m_debugDrawNode) {
            f->proxy->target = m_debugDrawNode;
            cmm::overlay::syncTo(f->proxy, m_debugDrawNode, this);
        }
        f->proxy2->setVisible(show && m_debugDrawNode2);
        if (show && m_debugDrawNode2) {
            f->proxy2->target = m_debugDrawNode2;
            cmm::overlay::syncTo(f->proxy2, m_debugDrawNode2, this);
        }
    }

    void startGame() {
        PlayLayer::startGame();
        bool demonOk = !cmm::val<bool>("autopracticemode.demons", false) || (m_level && m_level->m_demon.value() > 0);
        if (cmm::on("autopracticemode") && demonOk && !m_isPracticeMode) this->togglePracticeMode(true);
    }

    bool wantHitboxes() {
        return cmm::on("showhitboxes") || (cmm::on("hitboxesondeath") && m_playerDied);
    }

    // GD 2.2 only draws hitboxes when this says yes (normally: practice mode
    // with its own "show hitboxes" option on).
    bool shouldDrawDebugHitboxes() {
        return wantHitboxes() || PlayLayer::shouldDrawDebugHitboxes();
    }

    void drawHitboxesNow() {
        m_isDebugDrawEnabled = true;
        this->updateDebugDraw();
        if (m_debugDrawNode) m_debugDrawNode->setVisible(true);
        m_fields->drawingHitboxes = true;
        showProxy(true);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool wasDead = m_playerDied;
        PlayLayer::destroyPlayer(player, object);
        // GD stops the normal per-frame update after death, so draw right away.
        if (!wasDead && m_playerDied && object != m_anticheatSpike && cmm::on("hitboxesondeath")) drawHitboxesNow();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        auto f = m_fields.self();
        if (f->drawingHitboxes && !cmm::on("showhitboxes") && !PlayLayer::shouldDrawDebugHitboxes()) {
            m_isDebugDrawEnabled = false;
            if (m_debugDrawNode) {
                m_debugDrawNode->clear();
                m_debugDrawNode->setVisible(false);
            }
            f->drawingHitboxes = false;
            showProxy(false);
        }
    }

    void updateProgressbar() {
        PlayLayer::updateProgressbar();
        if (cmm::on("bestcolor") && m_percentageLabel && m_level && !m_isPlatformer) {
            // Best Color: colour the % while this attempt is past your best.
            int best = m_isPracticeMode ? m_level->m_practicePercent : m_level->m_normalPercent.value();
            bool newBest = this->getCurrentPercent() > static_cast<float>(best);
            m_percentageLabel->setColor(newBest ? cmm::bestColor() : ccColor3B{255, 255, 255});
        }
        if (!m_percentageLabel || m_isPlatformer) return;
        bool accurate = cmm::on("accuratepercentage");
        bool classic = cmm::on("classicpercentage");
        bool withBest = cmm::on("bestinpercentage");
        if (!accurate && !classic && !withBest) return;

        // Classic: by position in the level (before 2.2), not by time.
        float percent = classic && m_levelLength > 0.f
            ? m_player1->getPositionX() / m_levelLength * 100.f
            : this->getCurrentPercent();
        percent = std::clamp(percent, 0.f, 100.f);
        int decimals = accurate ? std::clamp(cmm::val<int>("accuratepercentage.decimals", 2), 0, 6) : 0;
        std::string text = accurate ? fmt::format("{:.{}f}%", percent, decimals) : fmt::format("{}%", static_cast<int>(percent));
        if (withBest && m_level) {
            int best = m_isPracticeMode ? m_level->m_practicePercent : m_level->m_normalPercent.value();
            text += fmt::format(" / {}%", best);
        }
        m_percentageLabel->setString(text.c_str());
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto f = m_fields.self();
        if (wantHitboxes()) {
            this->drawHitboxesNow();
        } else if (f->drawingHitboxes && !m_playerDied && !PlayLayer::shouldDrawDebugHitboxes()) {
            m_isDebugDrawEnabled = false;
            if (m_debugDrawNode) {
                m_debugDrawNode->clear();
                m_debugDrawNode->setVisible(false);
            }
            f->drawingHitboxes = false;
            showProxy(false);
        }
    }
};

class $modify(CMMEndShake, GJBaseGameLayer) {
    void shakeCamera(float duration, float strength, float interval) {
        if (cmm::on("noendshake") && m_levelEndAnimationStarted) return;
        GJBaseGameLayer::shakeCamera(duration, strength, interval);
    }
};

class $modify(CMMParticles, CCParticleSystemQuad) {
    void draw() {
        if (cmm::on("noparticles") && PlayLayer::get()) return;
        CCParticleSystemQuad::draw();
    }
};

class $modify(CMMGlow, GameObject) {
    void addGlow(std::string frame) {
        if (cmm::on("noglow")) return;
        GameObject::addGlow(std::move(frame));
    }
};

class $modify(CMMPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if (cmm::on("hidepause")) this->setVisible(false);
    }
};
