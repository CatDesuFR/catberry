// Developer test of the pause screen. Only runs if "selftest-pause.txt" exists
// in the mod's save folder (deleted when it starts). Opens main level 1,
// pauses, logs ("PAUSETEST") every menu on the pause screen with its buttons
// and size, saves a screenshot, scrolls the list if there is one, and leaves.
#include <core/Hacks.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <filesystem>

using namespace geode::prelude;

namespace {
    bool s_active = false;
    bool s_oldHitboxes = false;
    int s_frames = 0;
    GJGameLevel* s_level = nullptr;

    std::filesystem::path saveDir() { return std::filesystem::path(Mod::get()->getSaveDir().native()); }

    void shot(char const* name) {
        auto size = CCDirector::get()->getWinSize();
        auto rt = CCRenderTexture::create(static_cast<int>(size.width), static_cast<int>(size.height));
        rt->begin();
        CCDirector::get()->getRunningScene()->visit();
        rt->end();
        auto img = rt->newCCImage(true);
        if (img) {
            img->saveToFile((saveDir() / name).string().c_str(), false);
            img->release();
        }
    }

    void dump(CCNode* node, int depth) {
        if (!node || depth > 3) return;
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
            auto box = child->boundingBox();
            log::info("PAUSETEST {}{} <{}> {} children, at ({:.0f},{:.0f}) size {:.0f}x{:.0f} scale {:.2f} visible {}",
                std::string(depth * 2, ' '), child->getID().empty() ? "?" : child->getID(), typeid(*child).name(),
                child->getChildrenCount(), box.origin.x, box.origin.y, box.size.width, box.size.height, child->getScale(), child->isVisible());
            if (typeinfo_cast<CCMenu*>(child) || child->getChildrenCount() < 30) dump(child, depth + 1);
        }
    }
}

// Runs check() twice a second; not tied to any scene, so pausing or
// changing scenes doesn't stop it.
struct PauseChecker : CCObject {
    void check(float);
};

class $modify(PauseTestLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!s_active || PlayLayer::get() != this) return;
        if (++s_frames == 480) this->pauseGame(true);
    }
};

class $modify(PauseTestMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        std::error_code ec;
        auto trigger = saveDir() / "selftest-pause.txt";
        if (s_active || !std::filesystem::exists(trigger, ec)) return true;
        std::filesystem::remove(trigger, ec);
        // Stereo Madness with hitboxes showing, paused two seconds in.
        auto level = GameLevelManager::get()->getMainLevel(1, false);
        if (!level) return true;
        level->retain();
        if (auto h = cmm::find("showhitboxes")) { s_oldHitboxes = h->enabled; h->enabled = true; }
        s_level = level;
        s_active = true;
        s_frames = 0;
        Loader::get()->queueInMainThread([] { CCDirector::get()->replaceScene(PlayLayer::scene(s_level, false)); });
        // Look at the pause screen once it's up (the level is paused, so use the scheduler).
        static PauseChecker* s_checker = [] { auto c = new PauseChecker(); return c; }();
        CCScheduler::get()->scheduleSelector(schedule_selector(PauseChecker::check), s_checker, 0.5f, false);
        return true;
    }
};

void PauseChecker::check(float) {
    {
        static int s_checks = 0;
        if (!s_active) return;
        auto scene = CCDirector::get()->getRunningScene();
        auto pause = scene ? scene->getChildByType<PauseLayer>(0) : nullptr;
        if (!pause) { if (++s_checks > 60) s_active = false; return; }
        static int s_step = 0;
        s_step++;
        if (s_step == 2) {
            auto win = CCDirector::get()->getWinSize();
            log::info("PAUSETEST window {:.0f}x{:.0f}", win.width, win.height);
            dump(pause, 0);
            shot("selftest-pause.png");
        }
        if (s_step == 3) {
            // Pause Zoom: six notches of the wheel towards you (zoom in).
            for (int i = 0; i < 6; i++) CCDirector::get()->getMouseDispatcher()->dispatchScrollMSG(-1.f, 0.f);
            auto pl = PlayLayer::get();
            log::info("PAUSETEST zoomed: level scale {:.2f}, pause menu visible {}", pl ? pl->getScale() : 0.f, pause->isVisible());
        }
        if (s_step == 4) {
            shot("selftest-pause-zoomed.png");
            for (int i = 0; i < 20; i++) CCDirector::get()->getMouseDispatcher()->dispatchScrollMSG(1.f, 0.f);
            auto pl = PlayLayer::get();
            log::info("PAUSETEST zoomed back out: level scale {:.2f}, pause menu visible {}", pl ? pl->getScale() : 0.f, pause->isVisible());
            if (auto h = cmm::find("showhitboxes")) h->enabled = s_oldHitboxes;
            s_active = false;
            log::info("PAUSETEST DONE");
            CCScheduler::get()->unscheduleSelector(schedule_selector(PauseChecker::check), this);
        }
    }
}
