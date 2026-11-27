// Developer test of the menu: windows taller than the screen scroll, and
// dragging a window's body scrolls it (instead of moving the window). Only
// runs if "selftest-menu.txt" exists in the mod's save folder. Writes
// "MENUTEST" lines; the menu scale is put back.
#include <core/Hacks.hpp>
#include <gui/Menu.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using namespace geode::prelude;

namespace {
    float s_oldScale = 1.f;
    // Moves the real (Windows) mouse to a point of the game window, in
    // ImGui's coordinates (the window's client pixels), and presses or lets go.
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
        if (button == 0) in.mi.dx = in.mi.dy = 0;
        SendInput(1, &in, sizeof(in));
    }
    struct MenuRunner : CCNode {
        int frame = 0;
        ImVec2 start, startPos;
        void tick(float) {
            auto& io = ImGui::GetIO();
            auto win = ImGui::FindWindowByName("Level");
            frame++;
            if (frame == 30) {
                if (!win) { log::info("MENUTEST no Level window"); finish(); return; }
                log::info("MENUTEST Level window: height {:.0f} of screen {:.0f}, content {:.0f}, scroll max {:.0f}", win->Size.y, io.DisplaySize.y, win->ContentSize.y, win->ScrollMax.y);
                start = {win->Pos.x + win->Size.x * 0.85f, win->Pos.y + win->Size.y * 0.7f};
                startPos = win->Pos;
                realMouse(start, 0);
            }
            if (frame == 34) realMouse(start, 1);
            if (frame > 34 && frame <= 54) realMouse({start.x, start.y - (frame - 34) * 12.f}, 0);
            if (frame == 56) realMouse({start.x, start.y - 240.f}, -1);
            if (frame == 40 || frame == 50) log::info("MENUTEST mouse at ({:.0f},{:.0f}) down {}", io.MousePos.x, io.MousePos.y, io.MouseDown[0]);
            if (frame == 64) {
                log::info("MENUTEST after dragging 240 px up: scroll {:.0f}, window moved {:.0f} px", win->Scroll.y, std::abs(win->Pos.y - startPos.y) + std::abs(win->Pos.x - startPos.x));
                finish();
            }
        }
        void finish() {
            cmm::val<float>("menu.scale", 1.f) = s_oldScale;
            cmm::gui::applyTheme();
            if (cmm::gui::isOpen()) cmm::gui::toggle();
            log::info("MENUTEST DONE");
            this->unscheduleAllSelectors();
            this->removeFromParent();
        }
    };
}

class $modify(MenuTestMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        std::error_code ec;
        auto trigger = std::filesystem::path(Mod::get()->getSaveDir().native()) / "selftest-menu.txt";
        if (!std::filesystem::exists(trigger, ec)) return true;
        std::filesystem::remove(trigger, ec);
        s_oldScale = cmm::val<float>("menu.scale", 1.f);
        cmm::val<float>("menu.scale", 1.f) = 2.5f;  // tall windows
        cmm::gui::applyTheme();
        if (!cmm::gui::isOpen()) cmm::gui::toggle();
        auto runner = new MenuRunner();
        runner->autorelease();
        this->addChild(runner);
        runner->schedule(schedule_selector(MenuRunner::tick));
        return true;
    }
};
