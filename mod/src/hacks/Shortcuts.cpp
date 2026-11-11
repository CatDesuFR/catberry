#include <core/Hacks.hpp>
#include <core/Log.hpp>

using namespace geode::prelude;

namespace cmm::more { void requestFrameStep(); }

namespace cmm {
    namespace {
        PlayLayer* needLevel() {
            auto pl = PlayLayer::get();
            if (!pl) log::warn("That shortcut only works inside a level");
            return pl;
        }
    }

    void registerShortcuts() {
        using enum Tab;
        constexpr auto OK = PC::Allowed;

        auto shortcut = [](char const* id, char const* name, char const* desc, std::function<void()> fn) {
            Hack h{id, name, desc, Shortcuts, OK};
            h.action = std::move(fn);
            add(std::move(h));
        };

        shortcut("sc.restart", "Restart Level", "Restarts the current attempt.", [] {
            if (auto pl = needLevel()) pl->resetLevel();
        });
        shortcut("sc.fullrestart", "Full Restart", "Restarts from the start, clearing checkpoints.", [] {
            if (auto pl = needLevel()) pl->fullReset();
        });
        shortcut("sc.practice", "Toggle Practice Mode", "Turns practice mode on or off.", [] {
            if (auto pl = needLevel()) pl->togglePracticeMode(!pl->m_isPracticeMode);
        });
        shortcut("sc.placecp", "Place Checkpoint", "Places a checkpoint (practice mode only).", [] {
            if (auto pl = needLevel(); pl && pl->m_isPracticeMode) pl->markCheckpoint();
        });
        shortcut("sc.removecp", "Remove Checkpoint", "Removes the last checkpoint.", [] {
            if (auto pl = needLevel(); pl && pl->m_isPracticeMode) pl->removeCheckpoint(false);
        });
        shortcut("sc.step", "Step One Tick", "Frame Stepper: runs one physics tick (or press F).", [] {
            cmm::more::requestFrameStep();
        });
        shortcut("sc.password", "Show Level Password", "Shows the copy password of the current level.", [] {
            auto pl = needLevel();
            if (!pl || !pl->m_level) return;
            int pass = pl->m_level->m_password.value();
            std::string text = pass == 0 ? "This level can't be copied." : pass == 1 ? "Free to copy." : fmt::format("Password: {}", pass % 1000000);
            FLAlertLayer::create("Level Password", text, "OK")->show();
        });
        shortcut("sc.uncomplete", "Reset Level Progress", "Sets your best % on the current level back to 0.", [] {
            auto pl = needLevel();
            if (!pl || !pl->m_level) return;
            Ref<GJGameLevel> level = pl->m_level;
            createQuickPopup("Reset Progress", "Reset your progress on this level to 0%?", "Cancel", "Reset", [level](FLAlertLayer*, bool yes) {
                if (!yes) return;
                level->m_normalPercent = 0;
                level->m_newNormalPercent2 = 0;
                level->m_practicePercent = 0;
                level->m_orbCompletion = 0;
                log::info("Reset progress on {}", std::string(level->m_levelName));
            });
        });
        shortcut("sc.savefolder", "Open Save Folder", "Opens GD's save folder.", [] {
            utils::file::openFolder(dirs::getSaveDir());
        });
        shortcut("sc.modfolder", "Open Mod Folder", "Opens this mod's folder (CBF-LOGS.txt lives here).", [] {
            utils::file::openFolder(Mod::get()->getSaveDir());
        });
    }
}
