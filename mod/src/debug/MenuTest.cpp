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

using namespace geode::prelude;

namespace {
    float s_oldScale = 1.f;
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
                io.AddMousePosEvent(start.x, start.y);
            }
            if (frame == 32) io.AddMouseButtonEvent(0, true);
            if (frame > 32 && frame <= 52) io.AddMousePosEvent(start.x, start.y - (frame - 32) * 12.f);
            if (frame == 54) io.AddMouseButtonEvent(0, false);
            if (frame == 60) {
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
