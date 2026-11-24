// Hitbox Trail: draws the player's hitbox for every physics tick.
// Green = the tick you started clicking, blue = the tick you let go.
#include <core/Compat.hpp>
#include <core/Hacks.hpp>
#include <core/Players.hpp>
#include <features/WorldOverlay.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <deque>

using namespace geode::prelude;

namespace {
    enum class Mark { None, Press, Release };

    struct Box {
        CCRect rect;
        Mark mark;
    };

    bool s_pressed[2] = {false, false};
    bool s_released[2] = {false, false};
}

class $modify(TrailPlayLayer, PlayLayer) {
    struct Fields {
        Ref<CCDrawNode> node;
        std::deque<Box> boxes[2];
        unsigned lastProgress = ~0u;
    };

    bool init(CMM_PL_INIT_PARAMS) {
        if (!PlayLayer::init(CMM_PL_INIT_ARGS)) return false;
        auto node = CCDrawNode::create();
        node->setID("cats-mod-menu-hitbox-trail");
        // On the PlayLayer, lined up with the level each frame (see WorldOverlay.hpp).
        this->addChild(node, 999);
        m_fields->node = node;
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        auto f = m_fields.self();
        for (auto& b : f->boxes) b.clear();
        if (f->node) f->node->clear();
    }

    void recordTick() {
        if (!cmm::on("hitboxtrail")) return;

        auto f = m_fields.self();
        if (m_gameState.m_currentProgress == f->lastProgress) return;
        f->lastProgress = m_gameState.m_currentProgress;

        size_t maxBoxes = static_cast<size_t>(std::clamp(cmm::val<int>("hitboxtrail.length", 240), 10, 2000));
        for (int idx = 0; idx < 2; idx++) {
            auto p = idx == 0 ? m_player1 : m_player2;
            if (!p || !p->isVisible() || p->m_isDead) continue;
            Mark mark = s_pressed[idx] ? Mark::Press : s_released[idx] ? Mark::Release : Mark::None;
            f->boxes[idx].push_back({p->getObjectRect(), mark});
            while (f->boxes[idx].size() > maxBoxes) f->boxes[idx].pop_front();
            s_pressed[idx] = s_released[idx] = false;
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto f = m_fields.self();
        if (!f->node) return;
        f->node->clear();
        if (!cmm::on("hitboxtrail")) return;
        // Only part of the hitbox view: shown while hitboxes are.
        bool showing = cmm::on("showhitboxes") || (cmm::on("hitboxesondeath") && m_playerDied);
        if (!showing) return;
        cmm::overlay::syncTo(f->node, m_debugDrawNode ? static_cast<CCNode*>(m_debugDrawNode) : m_objectLayer, this);

        ccColor4F normal{1.f, 1.f, 1.f, 0.35f};
        ccColor4F press{0.2f, 1.f, 0.3f, 1.f};
        ccColor4F release{0.25f, 0.55f, 1.f, 1.f};
        ccColor4F clear{0.f, 0.f, 0.f, 0.f};

        for (auto& list : f->boxes) {
            for (auto const& box : list) {
                auto const& r = box.rect;
                CCPoint verts[4] = {
                    {r.getMinX(), r.getMinY()}, {r.getMaxX(), r.getMinY()},
                    {r.getMaxX(), r.getMaxY()}, {r.getMinX(), r.getMaxY()},
                };
                auto& color = box.mark == Mark::Press ? press : box.mark == Mark::Release ? release : normal;
                float width = box.mark == Mark::None ? 0.35f : 0.8f;
                f->node->drawPolygon(verts, 4, clear, width, color);
            }
        }
    }
};

class $modify(TrailGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        if (button != 1) return;
        int idx = cmm::forPlayer2(!isPlayer1) ? 1 : 0;
        (down ? s_pressed : s_released)[idx] = true;
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        auto pl = PlayLayer::get();
        if (pl && static_cast<GJBaseGameLayer*>(pl) == this) static_cast<TrailPlayLayer*>(pl)->recordTick();
    }
};

