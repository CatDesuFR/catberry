// Batch 3: No Shaders, No Portal Lightning, Lock Cursor, Show Taps,
// and the red noclip death flash.
#include <core/Compat.hpp>
#include <core/Hacks.hpp>
#include <gui/Menu.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

using namespace geode::prelude;

namespace cmm::visual {
    // Called by the noclip hook when noclip saves the player.
    void noclipFlash();
}

namespace {
    bool s_cursorLocked = false;

    void setCursorLock(bool lock) {
#ifdef GEODE_IS_WINDOWS
        if (lock == s_cursorLocked) return;
        if (lock) {
            HWND hwnd = GetForegroundWindow();
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (!hwnd || pid != GetCurrentProcessId()) return;
            RECT rect;
            GetClientRect(hwnd, &rect);
            POINT tl{rect.left, rect.top}, br{rect.right, rect.bottom};
            ClientToScreen(hwnd, &tl);
            ClientToScreen(hwnd, &br);
            RECT screen{tl.x, tl.y, br.x, br.y};
            ClipCursor(&screen);
        } else {
            ClipCursor(nullptr);
        }
        s_cursorLocked = lock;
#endif
    }
}

class $modify(CMMVisualGameLayer, GJBaseGameLayer) {
    void updateShaderLayer(float dt) {
        if (cmm::on("noshaders")) return;
        GJBaseGameLayer::updateShaderLayer(dt);
    }

    void lightningFlash(CCPoint from, CCPoint to, ccColor3B color, float lineWidth, float duration, int displacement, bool flash, float opacity) {
        if (cmm::on("noportallightning")) return;
        GJBaseGameLayer::lightningFlash(from, to, color, lineWidth, duration, displacement, flash, opacity);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        auto pl = PlayLayer::get();
        if (!down || !cmm::on("showtaps") || !pl || static_cast<GJBaseGameLayer*>(pl) != this) return;

        auto node = CCDrawNode::create();
        float radius = cmm::val<float>("showtaps.size", 12.f);
        auto color = isPlayer1 ? ccc4f(1.f, 1.f, 1.f, 0.6f) : ccc4f(0.4f, 0.8f, 1.f, 0.6f);
        node->drawDot({0, 0}, radius, color);
        node->setPosition(getMousePos());
        node->setZOrder(1002);
        pl->addChild(node);
        node->runAction(CCSequence::create(CCScaleTo::create(0.25f, 0.f), CCRemoveSelf::create(), nullptr));
    }
};

class $modify(CMMVisualPlayLayer, PlayLayer) {
    struct Fields {
        Ref<CCLayerColor> tint;
        float tintAlpha = 0.f;
    };

    bool init(CMM_PL_INIT_PARAMS) {
        if (!PlayLayer::init(CMM_PL_INIT_ARGS)) return false;
        auto tint = CCLayerColor::create({255, 0, 0, 0});
        tint->setZOrder(999);
        this->addChild(tint);
        m_fields->tint = tint;
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto f = m_fields.self();
        if (f->tint) {
            // Noclip flash colour (hex in the Noclip options).
            unsigned rgb = 0xFF0000;
            try { rgb = std::stoul(cmm::val<std::string>("noclip.color", "FF0000"), nullptr, 16); } catch (...) {}
            f->tint->setColor({static_cast<GLubyte>(rgb >> 16), static_cast<GLubyte>(rgb >> 8), static_cast<GLubyte>(rgb)});
            f->tintAlpha = std::max(0.f, f->tintAlpha - dt * 4.f);
            f->tint->setOpacity(static_cast<GLubyte>(f->tintAlpha * 255.f));
        }

        bool lock = cmm::on("lockcursor") && !m_isPaused && !m_playerDied && !cmm::gui::isOpen();
        setCursorLock(lock);
    }

    void onQuit() {
        setCursorLock(false);
        PlayLayer::onQuit();
    }

    void pauseGame(bool unfocused) {
        setCursorLock(false);
        PlayLayer::pauseGame(unfocused);
    }
};

namespace cmm::visual {
    void noclipFlash() {
        if (!cmm::val<bool>("noclip.tint", true)) return;
        auto pl = static_cast<CMMVisualPlayLayer*>(PlayLayer::get());
        if (!pl) return;
        pl->m_fields->tintAlpha = std::clamp(cmm::val<float>("noclip.tintopacity", 0.3f), 0.f, 1.f);
    }
}
