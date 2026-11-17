// Developer test of Speedhack music, the menu key and the CPS label. Only runs
// if "selftest-misc.txt" exists in the mod's save folder (deleted when it
// starts). Plays main level 1 and logs ("MISCTEST") what it finds.
#include <core/Hacks.hpp>
#include <gui/Menu.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/fmod/fmod.hpp>
#include <filesystem>

using namespace geode::prelude;

namespace {
    bool s_active = false;
    int s_frames = 0;
    GJGameLevel* s_level = nullptr;
    std::vector<std::pair<std::string, bool>> s_saved;
    float s_oldSpeed = 1.f;
    int s_oldKey = 0;
    std::filesystem::path saveDir() { return std::filesystem::path(Mod::get()->getSaveDir().native()); }
    void setHack(std::string_view id, bool on) { if (auto h = cmm::find(id); h && h->enabled != on) cmm::setEnabled(*h, on); }

    float effectivePitch(FMOD::ChannelGroup* group) {
        float total = 1.f;
        for (int i = 0; group && i < 8; i++) {
            float p = 1.f;
            group->getPitch(&p);
            total *= p;
            FMOD::ChannelGroup* parent = nullptr;
            if (group->getParentGroup(&parent) != FMOD_OK) break;
            group = parent;
        }
        return total;
    }
}

class $modify(MiscTestLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!s_active || PlayLayer::get() != this) return;
        if (m_gameState.m_currentProgress == 0) return;
        s_frames++;
        auto fmod = FMODAudioEngine::sharedEngine();
        if (s_frames == 240) {
            log::info("MISCTEST speedhack off: music plays at {:.2f}x", effectivePitch(fmod->m_backgroundMusicChannel));
            cmm::val<float>("speedhack.value", 1.f) = 2.f;
            cmm::val<bool>("speedhack.audio", true) = true;
            setHack("speedhack", true);
        }
        if (s_frames == 480) {
            log::info("MISCTEST speedhack 2x with Speedhack music: music plays at {:.2f}x", effectivePitch(fmod->m_backgroundMusicChannel));
            setHack("speedhack", false);
        }
        if (s_frames == 600) {
            log::info("MISCTEST speedhack off again: music plays at {:.2f}x", effectivePitch(fmod->m_backgroundMusicChannel));
            // Menu key: set to M, press M.
            cmm::val<int>("menu.key", KEY_Tab) = KEY_M;
            bool before = cmm::gui::isOpen();
            CCKeyboardDispatcher::get()->dispatchKeyboardMSG(KEY_M, true, false, 0.0);
            CCKeyboardDispatcher::get()->dispatchKeyboardMSG(KEY_M, false, false, 0.0);
            bool after = cmm::gui::isOpen();
            CCKeyboardDispatcher::get()->dispatchKeyboardMSG(KEY_Tab, true, false, 0.0);
            CCKeyboardDispatcher::get()->dispatchKeyboardMSG(KEY_Tab, false, false, 0.0);
            log::info("MISCTEST menu key M: menu {} -> {} after M, {} after Tab", before ? "open" : "closed", after ? "open" : "closed",
                cmm::gui::isOpen() ? "open" : "closed");
            if (cmm::gui::isOpen() != before) cmm::gui::toggle();
        }
        // CPS: clicks at 600-700, then a picture of the labels.
        if (s_frames > 620 && s_frames < 700 && s_frames % 12 == 0) {
            this->handleButton(true, 1, true);
            this->handleButton(false, 1, true);
        }
        if (s_frames == 705) {
            auto size = CCDirector::get()->getWinSize();
            auto rt = CCRenderTexture::create(static_cast<int>(size.width), static_cast<int>(size.height));
            rt->begin();
            CCDirector::get()->getRunningScene()->visit();
            rt->end();
            if (auto img = rt->newCCImage(true)) {
                img->saveToFile((saveDir() / "selftest-misc.png").string().c_str(), false);
                img->release();
            }
            for (auto& [id, on] : s_saved) setHack(id, on);
            cmm::val<float>("speedhack.value", 1.f) = s_oldSpeed;
            cmm::val<int>("menu.key", KEY_Tab) = s_oldKey;
            s_active = false;
            log::info("MISCTEST DONE");
            this->onQuit();
        }
    }
};

class $modify(MiscTestMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        std::error_code ec;
        auto trigger = saveDir() / "selftest-misc.txt";
        if (s_active || !std::filesystem::exists(trigger, ec)) return true;
        std::filesystem::remove(trigger, ec);
        auto level = GameLevelManager::get()->getMainLevel(1, false);
        if (!level) return true;
        level->retain();
        s_level = level;
        for (auto& h : cmm::all()) s_saved.push_back({h.id, h.enabled});
        s_oldSpeed = cmm::val<float>("speedhack.value", 1.f);
        s_oldKey = cmm::val<int>("menu.key", KEY_Tab);
        setHack("noclip", true);
        setHack("speedhack", false);
        setHack("labels", true);
        setHack("label.cps", true);
        s_active = true;
        s_frames = 0;
        Loader::get()->queueInMainThread([] { CCDirector::get()->replaceScene(PlayLayer::scene(s_level, false)); });
        return true;
    }
};
