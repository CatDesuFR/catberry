// Pause Zoom: while the game is paused, the scroll wheel zooms into the level
// around the mouse pointer, hitboxes included (they're drawn on the level, so
// they zoom with it). The pause menu hides while zoomed in and comes back
// when you scroll all the way out; resuming puts the view back.
#include <core/Hacks.hpp>
#include <Geode/modify/CCMouseDispatcher.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {
    bool s_zoomed = false;
    CCPoint s_oldPos;
    float s_oldScale = 1.f;

    PauseLayer* pauseLayer() {
        auto scene = CCDirector::get()->getRunningScene();
        return scene ? scene->getChildByType<PauseLayer>(0) : nullptr;
    }

    void resetZoom(PlayLayer* pl) {
        if (!s_zoomed || !pl) return;
        pl->setScale(s_oldScale);
        pl->setPosition(s_oldPos);
        if (auto pause = pauseLayer()) pause->setVisible(true);
        s_zoomed = false;
    }
}

class $modify(CMMPauseZoomMouse, CCMouseDispatcher) {
    bool dispatchScrollMSG(float y, float x) {
        auto pl = PlayLayer::get();
        if (!cmm::on("pausezoom") || !pl || !pl->m_isPaused || !pauseLayer() || y == 0.f)
            return CCMouseDispatcher::dispatchScrollMSG(y, x);
        if (!s_zoomed) {
            s_oldScale = pl->getScale();
            s_oldPos = pl->getPosition();
            s_zoomed = true;
        }
        // Scrolling up (negative y) zooms in, around the mouse pointer.
        float scale = pl->getScale() / s_oldScale;
        float next = std::clamp(scale * (y < 0.f ? 1.15f : 1.f / 1.15f), 1.f, 10.f);
        auto mouse = geode::cocos::getMousePos();
        auto parent = pl->getParent();
        auto m = parent ? parent->convertToNodeSpace(mouse) : mouse;
        // Keep the point under the mouse where it is.
        auto local = (m - pl->getPosition()) / pl->getScale();
        pl->setScale(s_oldScale * next);
        pl->setPosition(m - local * pl->getScale());
        // Keep the screen filled: don't slide past the level's edges.
        auto win = CCDirector::get()->getWinSize();
        auto box = pl->boundingBox();
        auto pos = pl->getPosition();
        if (box.getMinX() > 0.f) pos.x -= box.getMinX();
        if (box.getMaxX() < win.width) pos.x += win.width - box.getMaxX();
        if (box.getMinY() > 0.f) pos.y -= box.getMinY();
        if (box.getMaxY() < win.height) pos.y += win.height - box.getMaxY();
        pl->setPosition(pos);
        if (next <= 1.001f) {
            resetZoom(pl);
        } else if (auto pause = pauseLayer()) {
            pause->setVisible(false);
        }
        return true;
    }
};

class $modify(CMMPauseZoomPlay, PlayLayer) {
    // The game runs again: back to the normal view.
    void postUpdate(float dt) {
        if (s_zoomed && !m_isPaused) resetZoom(this);
        PlayLayer::postUpdate(dt);
    }
    void resetLevel() {
        resetZoom(this);
        PlayLayer::resetLevel();
    }
    void onQuit() {
        resetZoom(this);
        PlayLayer::onQuit();
    }
};

namespace cmm {
    void registerPauseZoom() {
        Hack hack{"pausezoom", "Pause Zoom", "While paused, scroll to zoom into the level around the mouse (hitboxes too). The pause menu hides while zoomed in; scroll back out to see it again.", Tab::Level, PC::Allowed};
        hack.enabled = true;
        add(std::move(hack));
    }
}
