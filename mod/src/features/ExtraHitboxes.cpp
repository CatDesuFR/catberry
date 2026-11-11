// Hitboxes GD's own hitbox view leaves out: solid blocks, breakable blocks,
// slopes and coins/collectibles. Shown together with Show Hitboxes / Hitboxes
// on Death. Only objects on screen are drawn.
#include <core/Hacks.hpp>
#include <features/WorldOverlay.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <array>
#include <cmath>

using namespace geode::prelude;

namespace {
    enum class Kind { Solid, Breakable, Slope, Coin };

    struct Entry {
        float x;  // position at load, for finding what's on screen
        GameObject* obj;
        Kind kind;
    };

    bool kindOf(GameObject* o, Kind& kind) {
        switch (o->m_objectType) {
            case GameObjectType::Solid: kind = Kind::Solid; break;
            case GameObjectType::Breakable: kind = Kind::Breakable; break;
            case GameObjectType::Slope: kind = Kind::Slope; break;
            case GameObjectType::SecretCoin:
            case GameObjectType::UserCoin:
            case GameObjectType::Collectible: kind = Kind::Coin; break;
            default: return false;
        }
        // Solids you can't touch have no hitbox.
        return !(o->m_isNoTouch && kind != Kind::Coin);
    }

    // Thick, light outline plus a faint fill, so it shows on dark and blue backgrounds.
    // CCDrawNode blends with pre-multiplied alpha, so scale the colour too.
    ccColor4F fillOf(ccColor4F c) { return {c.r * 0.12f, c.g * 0.12f, c.b * 0.12f, 0.12f}; }

    void drawBox(CCDrawNode* node, CCRect const& r, ccColor4F col) {
        CCPoint v[4] = {{r.getMinX(), r.getMinY()}, {r.getMaxX(), r.getMinY()}, {r.getMaxX(), r.getMaxY()}, {r.getMinX(), r.getMaxY()}};
        node->drawPolygon(v, 4, fillOf(col), 1.f, col);
    }

    // A slope fills half its box. Unflipped it's high on the right.
    void drawSlope(CCDrawNode* node, GameObject* o, CCRect const& r, ccColor4F col) {
        float rot = o->getRotation();
        bool sideways = std::fmod(std::fabs(rot) + 45.f, 180.f) >= 90.f;
        float w = sideways ? r.size.height : r.size.width;
        float h = sideways ? r.size.width : r.size.height;
        CCPoint local[3] = {{-w / 2, -h / 2}, {w / 2, -h / 2}, {w / 2, h / 2}};
        float rad = -rot * static_cast<float>(M_PI) / 180.f;
        CCPoint c{r.getMidX(), r.getMidY()};
        CCPoint v[3];
        for (int i = 0; i < 3; i++) {
            float x = local[i].x * (o->m_isFlipX ? -1.f : 1.f);
            float y = local[i].y * (o->m_isFlipY ? -1.f : 1.f);
            v[i] = CCPoint(c.x + x * std::cos(rad) - y * std::sin(rad), c.y + x * std::sin(rad) + y * std::cos(rad));
        }
        node->drawPolygon(v, 3, fillOf(col), 1.f, col);
    }
}

namespace {
    void drawRotated(CCDrawNode* node, CCRect const& r, float degrees, ccColor4F col) {
        float rad = -degrees * static_cast<float>(M_PI) / 180.f;
        float w = r.size.width / 2, h = r.size.height / 2;
        CCPoint c{r.getMidX(), r.getMidY()};
        CCPoint local[4] = {{-w, -h}, {w, -h}, {w, h}, {-w, h}};
        CCPoint v[4];
        for (int i = 0; i < 4; i++) {
            v[i] = CCPoint(c.x + local[i].x * std::cos(rad) - local[i].y * std::sin(rad),
                           c.y + local[i].x * std::sin(rad) + local[i].y * std::cos(rad));
        }
        node->drawPolygon(v, 4, {0, 0, 0, 0}, 0.6f, col);
    }

    void drawCircle(CCDrawNode* node, CCPoint c, float radius, ccColor4F col) {
        constexpr int n = 28;
        CCPoint v[n];
        for (int i = 0; i < n; i++) {
            float a = 2.f * static_cast<float>(M_PI) * i / n;
            v[i] = CCPoint(c.x + radius * std::cos(a), c.y + radius * std::sin(a));
        }
        node->drawPolygon(v, n, {0, 0, 0, 0}, 0.6f, col);
    }

    // Every hitbox a player has: outer box, the small inner box GD uses
    // against solid blocks, the box turned with the player, and a circle.
    void drawPlayer(CCDrawNode* node, PlayerObject* p) {
        if (!p || !p->isVisible()) return;
        auto outer = p->getObjectRect();
        auto inner = p->getObjectRect(0.3f, 0.3f);
        drawBox(node, outer, {1.f, 0.25f, 0.25f, 1.f});
        node->drawPolygon(std::array<CCPoint, 4>{CCPoint(inner.getMinX(), inner.getMinY()), CCPoint(inner.getMaxX(), inner.getMinY()),
            CCPoint(inner.getMaxX(), inner.getMaxY()), CCPoint(inner.getMinX(), inner.getMaxY())}.data(), 4,
            {0, 0, 0, 0}, 0.6f, {0.3f, 0.55f, 1.f, 1.f});
        drawRotated(node, outer, p->getRotation(), {1.f, 0.55f, 0.75f, 1.f});
        drawCircle(node, {outer.getMidX(), outer.getMidY()}, outer.size.width / 2.f, {0.3f, 0.9f, 0.85f, 1.f});
    }
}

class $modify(ExtraHitboxLayer, PlayLayer) {
    struct Fields {
        std::vector<Entry> objects;
        Ref<CCDrawNode> node;
        bool iconHidden = false;
    };

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        auto f = m_fields.self();
        f->objects.clear();
        if (m_objects) {
            for (auto o : CCArrayExt<GameObject*>(m_objects)) {
                Kind kind;
                if (o && kindOf(o, kind)) f->objects.push_back({o->getPositionX(), o, kind});
            }
        }
        std::sort(f->objects.begin(), f->objects.end(), [](auto const& a, auto const& b) { return a.x < b.x; });
        auto node = CCDrawNode::create();
        node->setID("cats-mod-menu-extra-hitboxes");
        this->addChild(node, 998);
        f->node = node;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto f = m_fields.self();
        if (!f->node) return;
        f->node->clear();
        bool show = cmm::on("showhitboxes") || (cmm::on("hitboxesondeath") && m_playerDied);

        // Hide the player's icon while hitboxes are showing (option).
        // Only undoes its own hiding, so hide-player triggers still work.
        bool hideIcon = show && cmm::val<bool>("showhitboxes.hideplayer", false);
        if (hideIcon != f->iconHidden) {
            for (auto p : {m_player1, m_player2}) {
                if (p && p->m_mainLayer) p->m_mainLayer->setVisible(!hideIcon);
            }
            f->iconHidden = hideIcon;
        }
        if (!show) return;

        CCNode* space = m_debugDrawNode ? static_cast<CCNode*>(m_debugDrawNode) : m_objectLayer;
        cmm::overlay::syncTo(f->node, space, this);

        // What part of the level is on screen, with some margin.
        auto win = CCDirector::get()->getWinSize();
        float a = space->convertToNodeSpace({0, 0}).x;
        float b = space->convertToNodeSpace({win.width, 0}).x;
        float left = std::min(a, b) - 150.f, right = std::max(a, b) + 150.f;

        drawPlayer(f->node, m_player1);
        if (m_player2 && (m_gameState.m_isDualMode || (m_levelSettings && m_levelSettings->m_twoPlayerMode))) drawPlayer(f->node, m_player2);

        ccColor4F solid{0.35f, 0.75f, 1.f, 1.f};
        ccColor4F breakable{0.7f, 0.6f, 1.f, 1.f};
        ccColor4F coin{1.f, 0.85f, 0.2f, 1.f};
        auto it = std::lower_bound(f->objects.begin(), f->objects.end(), left, [](Entry const& e, float x) { return e.x < x; });
        for (; it != f->objects.end() && it->x <= right; ++it) {
            auto o = it->obj;
            if (!o->getParent() || !o->isVisible()) continue;  // not in the level right now
            auto const& r = o->getObjectRect();
            switch (it->kind) {
                case Kind::Solid: drawBox(f->node, r, solid); break;
                case Kind::Breakable: drawBox(f->node, r, breakable); break;
                case Kind::Slope: drawSlope(f->node, o, r, solid); break;
                case Kind::Coin: drawBox(f->node, r, coin); break;
            }
        }
    }
};
