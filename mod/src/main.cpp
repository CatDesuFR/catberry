#include <Geode/Geode.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <core/Hacks.hpp>
#include <core/Log.hpp>
#include <features/StartPos.hpp>
#include <features/ClickBeforeFrames.hpp>
#include <features/Stats.hpp>

namespace cmm::more { void requestFrameStep(); }
#include <gui/Menu.hpp>
#include <imgui-cocos.hpp>

using namespace geode::prelude;

$on_mod(Loaded) {
    cmm::registerHacks();
    cmm::loadHacks();

    ImGuiCocos::get()
        .setup([] { cmm::gui::setup(); })
        .draw([] { cmm::gui::draw(); });

    cmm::log::info("Catberry's Mod Menu {} loaded with {} hacks. Press Tab to open.",
        Mod::get()->getVersion().toVString(), cmm::all().size());
}

class $modify(CMMKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool isKeyDown, bool isKeyRepeat, double timestamp) {
        bool typing = ImGuiCocos::get().isInitialized() && ImGui::GetIO().WantTextInput;
        if (isKeyDown) cmm::stats::heldKeys.insert(static_cast<int>(key));
        else cmm::stats::heldKeys.erase(static_cast<int>(key));

        if (isKeyDown && !isKeyRepeat && !typing) {
            if (cmm::gui::isOpen() && cmm::gui::captureBindKey(static_cast<int>(key))) return true;
            // Per-hack keybinds (set by right-clicking a hack in the menu).
            for (auto& hack : cmm::all()) {
                int bound = cmm::val<int>(fmt::format("bind.{}", hack.id), 0);
                if (bound == 0 || bound != static_cast<int>(key)) continue;
                if (hack.action) hack.action();
                else cmm::setEnabled(hack, !hack.enabled);
                return true;
            }
            if (key == KEY_Tab) {
                cmm::gui::toggle();
                return true;
            }
            if (key == KEY_F8 && cmm::on("cbf")) {
                cmm::cbf::cycleStage();
                return true;
            }
            if (auto pl = PlayLayer::get(); pl && cmm::on("startposswitcher")) {
                if (key == KEY_Q) { cmm::startpos::step(pl, -1); return true; }
                if (key == KEY_E) { cmm::startpos::step(pl, 1); return true; }
            }
            if (PlayLayer::get() && cmm::on("framestepper") && key == KEY_F) {
                cmm::more::requestFrameStep();
                return true;
            }
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, isKeyDown, isKeyRepeat, timestamp);
    }
};
