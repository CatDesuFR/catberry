// Developer test of the editor additions. Only runs if "selftest-editor.txt"
// exists in the mod's save folder (deleted when it starts). Opens a small level
// (flat, one spike) in the editor and checks: Enter plays/pauses/resumes the
// playtest, Noclip gets past the spike (and without it the playtest ends
// there), Show Hitboxes draws in the playtest, Jump Keys (an extra key jumps,
// Space turned off doesn't) and Slider Limit Bypass. Writes "EDITORTEST" lines
// to the Geode log; your settings are put back.
#include <core/Hacks.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <filesystem>

using namespace geode::prelude;

namespace cmm::jumpkeys {
    bool handle(int key, bool down, bool repeat);
    std::vector<int> extra();
    void setExtra(std::vector<int> const& keys);
}

namespace {
    std::vector<std::pair<std::string, bool>> s_saved;
    std::string s_savedKeys;
    bool s_savedSpace = true;

    std::filesystem::path saveDir() { return std::filesystem::path(Mod::get()->getSaveDir().native()); }
    void setHack(std::string_view id, bool on) {
        if (auto h = cmm::find(id); h && h->enabled != on) h->enabled = on;
    }
    char const* mode(LevelEditorLayer* ed) {
        switch (ed->m_playbackMode) {
            case PlaybackMode::Playing: return "playing";
            case PlaybackMode::Paused: return "paused";
            default: return "stopped";
        }
    }

    void shot(char const* name) {
        auto size = CCDirector::get()->getWinSize();
        auto rt = CCRenderTexture::create(static_cast<int>(size.width), static_cast<int>(size.height));
        rt->begin();
        CCDirector::get()->getRunningScene()->visit();
        rt->end();
        if (auto img = rt->newCCImage(true)) {
            img->saveToFile((saveDir() / name).string().c_str(), false);
            img->release();
        }
    }

    // The same Jump Keys checks in a normal level.
    struct PlayRunner : CCNode {
        int step = 0;
        float wait = 1.5f, groundY = 0.f;
        void tick(float dt) {
            auto pl = PlayLayer::get();
            if (!pl || !pl->m_player1) return;
            if ((wait -= dt) > 0.f) return;
            auto kd = CCKeyboardDispatcher::get();
            switch (step++) {
                case 0:
                    cmm::jumpkeys::setExtra({static_cast<int>(KEY_J)});
                    cmm::save<bool>("jumpkeys.space", false);
                    setHack("jumpkeys", true);
                    setHack("noclip", true);
                    groundY = pl->m_player1->getPositionY();
                    kd->dispatchKeyboardMSG(KEY_Space, true, false, 0.0);
                    wait = 0.25f;
                    break;
                case 1:
                    log::info("EDITORTEST level, Space turned off: {:.0f} above the ground", pl->m_player1->getPositionY() - groundY);
                    kd->dispatchKeyboardMSG(KEY_Space, false, false, 0.0);
                    kd->dispatchKeyboardMSG(KEY_J, true, false, 0.0);
                    wait = 0.25f;
                    break;
                case 2:
                    log::info("EDITORTEST level, extra key J: {:.0f} above the ground (x {:.0f}, on ground {}, mode ship {} ball {})", pl->m_player1->getPositionY() - groundY,
                        pl->m_player1->getPositionX(), pl->m_player1->m_isOnGround, pl->m_player1->m_isShip, pl->m_player1->m_isBall);
                    kd->dispatchKeyboardMSG(KEY_J, false, false, 0.0);
                    wait = 0.8f;
                    break;
                case 3:
                    cmm::save<bool>("jumpkeys.space", true);
                    groundY = pl->m_player1->getPositionY();
                    kd->dispatchKeyboardMSG(KEY_Space, true, false, 0.0);
                    wait = 0.25f;
                    break;
                case 4:
                    log::info("EDITORTEST level, Space turned on: {:.0f} above the ground", pl->m_player1->getPositionY() - groundY);
                    kd->dispatchKeyboardMSG(KEY_Space, false, false, 0.0);
                    pl->handleButton(true, 1, true);
                    groundY = pl->m_player1->getPositionY();
                    wait = 0.25f;
                    break;
                case 5:
                    log::info("EDITORTEST level, handleButton directly: {:.0f} above the ground; x {:.0f}, progress {}, holding {}, yVel {:.2f}, paused {}, dead {}",
                        pl->m_player1->getPositionY() - groundY, pl->m_player1->getPositionX(), pl->m_gameState.m_currentProgress,
                        pl->m_player1->m_holdingButtons[1], pl->m_player1->m_yVelocity, pl->m_isPaused, pl->m_playerDied);
                    pl->handleButton(false, 1, true);
                    wait = 0.3f;
                    break;
                default:
                    for (auto& [id, on] : s_saved) setHack(id, on);
                    cmm::save<std::string>("jumpkeys.extra", s_savedKeys);
                    cmm::save<bool>("jumpkeys.space", s_savedSpace);
                    log::info("EDITORTEST DONE");
                    this->unscheduleAllSelectors();
                    pl->onQuit();
                    break;
            }
        }
    };
    GJGameLevel* s_testLevel = nullptr;

    struct Runner : CCNode {
        int step = 0;
        float wait = 0.f;
        float maxX = 0.f;
        bool sawDamage = false;
        float groundY = 0.f;

        void tick(float dt) {
            auto ed = LevelEditorLayer::get();
            if (!ed || !ed->m_editorUI) return;
            auto ui = ed->m_editorUI;
            if (ed->m_player1 && ed->m_playbackMode == PlaybackMode::Playing) maxX = std::max(maxX, ed->m_player1->getPositionX());
            if ((wait -= dt) > 0.f) return;
            switch (step++) {
                case 0:  // Enter: play, pause, play, stop
                    setHack("editorenterplaytest", true);
                    ui->keyDown(KEY_Enter, 0.0);
                    log::info("EDITORTEST Enter 1: {}", mode(ed));
                    wait = 0.3f;
                    break;
                case 1:
                    ui->keyDown(KEY_Enter, 0.0);
                    log::info("EDITORTEST Enter 2: {}", mode(ed));
                    wait = 0.3f;
                    break;
                case 2:
                    ui->keyDown(KEY_Enter, 0.0);
                    log::info("EDITORTEST Enter 3: {}", mode(ed));
                    wait = 0.3f;
                    break;
                case 3:
                    ui->onStopPlaytest(nullptr);
                    log::info("EDITORTEST stopped: {}", mode(ed));
                    // Noclip off: the spike should end the playtest.
                    setHack("noclip", false);
                    setHack("showhitboxes", false);
                    maxX = 0.f;
                    ui->onPlaytest(nullptr);
                    wait = 2.5f;
                    break;
                case 4:
                    log::info("EDITORTEST without noclip: {} after 2.5 s, got to x {:.0f}", mode(ed), maxX);
                    if (ed->m_playbackMode != PlaybackMode::Not) ui->onStopPlaytest(nullptr);
                    setHack("noclip", true);
                    setHack("showhitboxes", true);
                    maxX = 0.f;
                    ui->onPlaytest(nullptr);
                    wait = 1.0f;
                    break;
                case 5: {
                    bool wants = ed->shouldDrawDebugHitboxes();
                    auto node = ed->m_debugDrawNode;
                    log::info("EDITORTEST hitboxes in playtest: asked {}, draw node {}, visible {}", wants, node != nullptr,
                        node && node->isVisible());
                    shot("selftest-editor-hitboxes.png");
                    wait = 1.5f;
                    break;
                }
                case 6:
                    log::info("EDITORTEST with noclip: {} after 2.5 s, got to x {:.0f}", mode(ed), maxX);
                    // Jump Keys: J jumps, Space turned off.
                    cmm::jumpkeys::setExtra({static_cast<int>(KEY_J)});
                    cmm::save<bool>("jumpkeys.space", false);
                    setHack("jumpkeys", true);
                    if (ed->m_playbackMode != PlaybackMode::Playing) ui->onPlaytest(nullptr);
                    wait = 0.5f;
                    break;
                case 7: {
                    // Jump Keys through the real keyboard path: press, wait,
                    // see whether the player left the ground.
                    groundY = ed->m_player1->getPositionY();
                    CCKeyboardDispatcher::get()->dispatchKeyboardMSG(KEY_Space, true, false, 0.0);
                    wait = 0.25f;
                    break;
                }
                case 8: {
                    auto kd = CCKeyboardDispatcher::get();
                    log::info("EDITORTEST Space turned off: {:.0f} above the ground after pressing", ed->m_player1->getPositionY() - groundY);
                    kd->dispatchKeyboardMSG(KEY_Space, false, false, 0.0);
                    kd->dispatchKeyboardMSG(KEY_J, true, false, 0.0);
                    wait = 0.25f;
                    break;
                }
                case 9: {
                    auto kd = CCKeyboardDispatcher::get();
                    log::info("EDITORTEST extra key J: {:.0f} above the ground after pressing", ed->m_player1->getPositionY() - groundY);
                    kd->dispatchKeyboardMSG(KEY_J, false, false, 0.0);
                    wait = 0.8f;
                    break;
                }
                case 10: {
                    cmm::save<bool>("jumpkeys.space", true);
                    groundY = ed->m_player1->getPositionY();
                    CCKeyboardDispatcher::get()->dispatchKeyboardMSG(KEY_Space, true, false, 0.0);
                    wait = 0.25f;
                    break;
                }
                case 11: {
                    log::info("EDITORTEST Space turned on: {:.0f} above the ground after pressing", ed->m_player1->getPositionY() - groundY);
                    CCKeyboardDispatcher::get()->dispatchKeyboardMSG(KEY_Space, false, false, 0.0);
                    ui->onStopPlaytest(nullptr);
                    // Slider Limit Bypass: a slider dragged far past its end.
                    auto drag = [&](bool bypass) {
                        setHack("sliderlimit", bypass);
                        auto slider = Slider::create(this, nullptr, 1.f);
                        slider->setPosition({200.f, 200.f});
                        CCDirector::get()->getRunningScene()->addChild(slider, 1000);
                        slider->setValue(0.5f);
                        auto thumb = slider->getThumb();
                        auto at = thumb->getParent()->convertToWorldSpace(thumb->getPosition());
                        auto ui = CCDirector::get()->convertToUI(at);
                        auto touch = new CCTouch();
                        touch->autorelease();
                        touch->setTouchInfo(0, ui.x, ui.y);
                        auto logic = slider->m_touchLogic;
                        bool began = logic->ccTouchBegan(touch, nullptr);
                        touch->setTouchInfo(0, ui.x + 400.f, ui.y);
                        logic->ccTouchMoved(touch, nullptr);
                        logic->ccTouchEnded(touch, nullptr);
                        log::info("EDITORTEST slider dragged 400 px right ({}): began {}, value {:.2f}", bypass ? "bypass on" : "bypass off", began, thumb->getValue());
                        slider->removeFromParent();
                    };
                    drag(true);
                    drag(false);
                    wait = 0.3f;
                    break;
                }
                default: {
                    // On to a normal level for the same Jump Keys checks.
                    this->unscheduleAllSelectors();
                    Loader::get()->queueInMainThread([] {
                        // A main level (Stereo Madness): a level made from
                        // editor data here doesn't take input.
                        auto scene = PlayLayer::scene(GameLevelManager::get()->getMainLevel(1, false), false);
                        CCDirector::get()->replaceScene(scene);
                        auto runner = new PlayRunner();
                        runner->autorelease();
                        scene->addChild(runner);
                        runner->schedule(schedule_selector(PlayRunner::tick));
                    });
                    break;
                }
            }
        }
    };
}

class $modify(EditorTestMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        std::error_code ec;
        auto trigger = saveDir() / "selftest-editor.txt";
        if (!std::filesystem::exists(trigger, ec)) return true;
        std::filesystem::remove(trigger, ec);
        for (auto& h : cmm::all()) s_saved.push_back({h.id, h.enabled});
        s_savedKeys = cmm::val<std::string>("jumpkeys.extra", "");
        s_savedSpace = cmm::val<bool>("jumpkeys.space", true);
        auto level = GJGameLevel::create();
        level->retain();
        level->m_levelName = "CMM Editor Test";
        // Flat ground, a spike (object 8) at x 400.
        level->m_levelString = ZipUtils::compressString("kA2,0,kA4,0;1,8,2,405,3,15;1,1,2,6000,3,15;", false, 0, 0);
        level->m_levelType = GJLevelType::Editor;
        s_testLevel = level;
        log::info("EDITORTEST start");
        Loader::get()->queueInMainThread([level] {
            auto scene = LevelEditorLayer::scene(level, false);
            CCDirector::get()->replaceScene(scene);
            auto runner = new Runner();
            runner->autorelease();
            runner->wait = 2.f;
            scene->addChild(runner);
            runner->schedule(schedule_selector(Runner::tick));
        });
        return true;
    }
};
