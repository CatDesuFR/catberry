// Developer test of the pause screen. Only runs if "selftest-pause.txt" exists
// in the mod's save folder (deleted when it starts). Opens main level 1,
// pauses, logs ("PAUSETEST") every menu on the pause screen with its buttons
// and size, saves a screenshot, scrolls the list if there is one, and leaves.
#include <core/Hacks.hpp>
#include <gui/Menu.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using namespace geode::prelude;

namespace {
    bool s_active = false;
    bool s_oldHitboxes = false;
    bool s_oldPm = false;
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

    // The real (Windows) mouse, at a point in ImGui's coordinates.
    void realMouse(ImVec2 at, int button) {
        HWND hwnd = GetForegroundWindow();
        POINT p{static_cast<LONG>(at.x), static_cast<LONG>(at.y)};
        RECT rc{};
        if (hwnd && GetClientRect(hwnd, &rc) && rc.right > 0) {
            auto& io = ImGui::GetIO();
            p.x = static_cast<LONG>(at.x * rc.right / io.DisplaySize.x);
            p.y = static_cast<LONG>(at.y * rc.bottom / io.DisplaySize.y);
            ClientToScreen(hwnd, &p);
        }
        SetCursorPos(p.x, p.y);
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = button > 0 ? MOUSEEVENTF_LEFTDOWN : button < 0 ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_MOVE;
        SendInput(1, &in, sizeof(in));
    }
    ImVec2 s_dragStart;
    float s_oldMenuScale = 1.f;

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
        ++s_frames;
        // Practice Music Bypass: into practice mode, then is the level song playing?
        if (s_frames == 400) {
            auto pm = cmm::find("practicemusic");
            s_oldPm = pm && pm->enabled;
            if (pm) pm->enabled = true;
            this->togglePracticeMode(true);
        }
        if (s_frames == 600 || s_frames == 700) {
            auto fm = FMODAudioEngine::get();
            log::info("PAUSETEST practice: level song playing {}, practice song playing {}, song at {} ms, level time {:.2f}",
                fm->isMusicPlaying(m_level->getAudioFileName(), 0), fm->isMusicPlaying("StayInsideMe.mp3", 0), fm->getMusicTimeMS(0), m_gameState.m_levelTime);
            // Die and come back at the start: the song should follow.
            if (s_frames == 600) this->resetLevel();
        }
        if (s_frames == 800) this->pauseGame(true);
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
            // The wheel with the menu open scrolls the menu, not the level.
            cmm::gui::toggle();
            for (int i = 0; i < 6; i++) CCDirector::get()->getMouseDispatcher()->dispatchScrollMSG(-1.f, 0.f);
            log::info("PAUSETEST menu open + wheel: level scale {:.2f}", pl ? pl->getScale() : 0.f);
            cmm::gui::toggle();
            if (pl) pl->togglePracticeMode(false);
            if (auto pm = cmm::find("practicemusic")) pm->enabled = s_oldPm;
            // Swipe test next: tall menu windows.
            s_oldMenuScale = cmm::val<float>("menu.scale", 1.f);
            cmm::val<float>("menu.scale", 1.f) = 2.5f;
            cmm::gui::applyTheme();
            cmm::gui::toggle();
        }
        if (s_step == 6) {
            auto win = ImGui::FindWindowByName("Level");
            if (win) s_dragStart = {win->Pos.x + win->Size.x * 0.85f, win->Pos.y + win->Size.y * 0.7f};
            log::info("PAUSETEST swipe: Level window {}", win != nullptr);
            realMouse(s_dragStart, 0);
        }
        if (s_step == 7) realMouse(s_dragStart, 1);
        if (s_step >= 8 && s_step <= 11) realMouse({s_dragStart.x, s_dragStart.y - (s_step - 7) * 60.f}, 0);
        if (s_step == 12) realMouse({s_dragStart.x, s_dragStart.y - 240.f}, -1);
        if (s_step == 13) {
            auto win = ImGui::FindWindowByName("Level");
            auto pl = PlayLayer::get();
            log::info("PAUSETEST swipe 240 px up over the pause screen: scroll {:.0f}, still paused {}", win ? win->Scroll.y : -1.f, pl && pl->m_isPaused);
            cmm::gui::toggle();
            cmm::val<float>("menu.scale", 1.f) = s_oldMenuScale;
            cmm::gui::applyTheme();
            if (auto h = cmm::find("showhitboxes")) h->enabled = s_oldHitboxes;
            s_active = false;
            log::info("PAUSETEST DONE");
            CCScheduler::get()->unscheduleSelector(schedule_selector(PauseChecker::check), this);
        }
    }
}
