// Developer self-test. Only runs if a file called "selftest.txt" exists in
// the mod's save folder (it's deleted as soon as the test starts), and stops
// the moment you leave its level.
//
// It opens a test level, does scripted clicks through GD's own input path,
// and writes what happened to the Geode log ("SELFTEST" lines):
// - Show Hitboxes / Hitboxes on Death: saves screenshots of the game.
// - The hidden-player simulation: how far it is from the real player.
// - Click Before Frames: jumps over spike sets at different timings, off vs on.
// Nothing it changes is saved; your settings are put back afterwards.
#include <core/Hacks.hpp>
#include <features/ClickBeforeFrames.hpp>
#include <features/Replay.hpp>
#include <gui/Menu.hpp>
#include <features/Sim.hpp>
#include <features/Stats.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <cmath>
#include <typeinfo>
#include <filesystem>
#include <fstream>

using namespace geode::prelude;

namespace {
    bool s_active = false;
    bool s_started = false;
    GJGameLevel* s_level = nullptr;

    // Layout of the test level.
    constexpr float kFirstGroup = 1500.f;
    constexpr float kGroupGap = 600.f;
    constexpr int kGroups = 30;
    // How far before a spike set the jump is pressed (sweep), for CBF off then on.
    std::vector<int> const kDistances = {30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 90, 100};

    std::filesystem::path saveDir() {
        return std::filesystem::path(Mod::get()->getSaveDir().native());
    }

    // ---- temporary settings (never saved) ----
    std::vector<std::pair<std::string, bool>> s_oldHacks;
    int s_oldStage = 3;
    int s_oldLook = 120;
    bool s_oldLogfile = true;

    void setHack(std::string_view id, bool on) {
        auto h = cmm::find(id);
        if (!h) return;
        bool known = false;
        for (auto& [k, v] : s_oldHacks) known |= k == id;
        if (!known) s_oldHacks.push_back({h->id, h->enabled});
        h->enabled = on;
    }

    std::string s_oldReplay;
    float s_oldHbPlayer = 1.f;
    bool s_oldHide10 = true;
    bool s_replayTouched = false;

    void restoreSettings() {
        cmm::val<float>("hitboxmultiplier.player", 1.f) = s_oldHbPlayer;
        cmm::val<bool>("framecounter.hide10", true) = s_oldHide10;
        if (s_replayTouched) {
            cmm::cbf::setReplay(s_oldReplay);
            std::error_code ec;
            std::filesystem::remove(saveDir() / "macros" / "cmm-selftest.txt", ec);
            s_replayTouched = false;
        }
        for (auto& [id, on] : s_oldHacks) {
            if (auto h = cmm::find(id)) h->enabled = on;
        }
        s_oldHacks.clear();
        cmm::val<int>("cbf.stage", 3) = s_oldStage;
        cmm::val<int>("cbf.lookahead", 120) = s_oldLook;
        cmm::val<bool>("cbf.logfile", true) = s_oldLogfile;
    }

    void screenshot(char const* name) {
        auto size = CCDirector::get()->getWinSize();
        auto rt = CCRenderTexture::create(static_cast<int>(size.width), static_cast<int>(size.height));
        rt->begin();
        CCDirector::get()->getRunningScene()->visit();
        rt->end();
        auto img = rt->newCCImage(true);
        auto path = (saveDir() / name).string();
        bool ok = img && img->saveToFile(path.c_str(), false);
        if (img) img->release();
        log::info("SELFTEST screenshot {} -> {}", ok ? "saved" : "FAILED", path);
    }

    // When GD actually applies jump presses to player 1 (to line up timing).
    int64_t s_lastRealPush = -1;

    enum class Stage { Hitboxes, Accuracy, Sweep, DeathHitboxes, Done };

    struct Run {
        Stage stage = Stage::Hitboxes;
        int64_t lastTick = -1;
        int wait = 0;
        // accuracy
        int accPhase = 0;
        int groundTicks = 0;
        std::vector<CCPoint> simPath, realPath;
        int64_t accPressTick = -1;
        // sweep
        int group = 0;
        bool pressed = false;
        bool released = false;
        int deathsBefore = 0;
        int savedBefore = 0;
        int deathFrames = 0;
        int64_t sentAt = -1;
        int64_t pushAt = -1;  // first real press GD applied after sending
        bool shot = false;
    } s_run;
    int s_diagTicks = 1000;  // ticks logged at the start of each CBF pass
    int s_diagUpdates = 0;   // PlayerObject::update calls on player 1 since the pass started

    void finish() {
        s_run.stage = Stage::Done;
        restoreSettings();
        s_active = false;
        if (!cmm::gui::isOpen()) cmm::gui::toggle();  // for a screenshot of the menu
        log::info("SELFTEST DONE");
    }

    void accuracyTick(PlayLayer* pl, int64_t now) {
        auto real = pl->m_player1;
        switch (s_run.accPhase) {
            case 0:  // wait on the ground, then predict a tap and do the same tap for real
                s_run.groundTicks = real->m_isOnGround ? s_run.groundTicks + 1 : 0;
                if (s_run.groundTicks < 30) break;
                {
                    auto p = cmm::sim::scratch(pl, 1);
                    cmm::sim::copyState(p, real);
                    log::info("SELFTEST sim fields real/sim: pos ({:.2f},{:.2f})/({:.2f},{:.2f}) mpos ({:.2f},{:.2f})/({:.2f},{:.2f}) speed {}/{} mult {}/{} plat {}/{} locked {}/{} ctrlOff {}/{} ground {}/{} yvel {}/{}",
                        real->getPositionX(), real->getPositionY(), p->getPositionX(), p->getPositionY(),
                        real->m_position.x, real->m_position.y, p->m_position.x, p->m_position.y,
                        real->m_playerSpeed, p->m_playerSpeed, real->m_speedMultiplier, p->m_speedMultiplier,
                        real->m_isPlatformer, p->m_isPlatformer, real->m_isLocked, p->m_isLocked,
                        real->m_controlsDisabled, p->m_controlsDisabled, real->m_isOnGround, p->m_isOnGround,
                        real->m_yVelocity, p->m_yVelocity);
                    s_run.simPath.clear();
                    for (int k = 0; k < 140; k++) {
                        if (k == 0) p->pushButton(PlayerButton::Jump);
                        if (k == 1) p->releaseButton(PlayerButton::Jump);
                        if (!cmm::sim::step(pl, p)) {
                            log::info("SELFTEST accuracy: hidden player died at tick {} on flat ground (stopped={})", k, cmm::sim::stopped());
                            break;
                        }
                        s_run.simPath.push_back(p->getPosition());
                    }
                    cmm::sim::restoreTouched();
                }
                pl->handleButton(true, 1, true);
                s_run.released = false;
                s_run.accPressTick = now;
                s_run.realPath.clear();
                s_run.accPhase = 1;
                break;
            case 1:
                if (!s_run.released) {
                    pl->handleButton(false, 1, true);
                    s_run.released = true;
                }
                if (s_run.realPath.size() >= 145) {
                    // GD may apply the press a tick later than we sent it: try offsets.
                    int bestOffset = 0;
                    float bestMax = 1e9f, bestAvg = 0.f;
                    for (int off = -2; off <= 3; off++) {
                        float mx = 0.f, sum = 0.f;
                        int n = 0;
                        for (size_t i = 0; i < s_run.simPath.size(); i++) {
                            int j = static_cast<int>(i) + off;
                            if (j < 0 || j >= static_cast<int>(s_run.realPath.size())) continue;
                            float e = std::hypot(s_run.simPath[i].x - s_run.realPath[j].x, s_run.simPath[i].y - s_run.realPath[j].y);
                            mx = std::max(mx, e);
                            sum += e;
                            n++;
                        }
                        if (n > 100 && mx < bestMax) { bestMax = mx; bestAvg = sum / n; bestOffset = off; }
                    }
                    for (int i : {0, 1, 2, 3, 10, 50, 100}) {
                        if (i < static_cast<int>(s_run.simPath.size()) && i < static_cast<int>(s_run.realPath.size())) {
                            log::info("SELFTEST path tick {}: sim ({:.2f},{:.2f}) real ({:.2f},{:.2f})", i,
                                s_run.simPath[i].x, s_run.simPath[i].y, s_run.realPath[i].x, s_run.realPath[i].y);
                        }
                    }
                    log::info("SELFTEST accuracy: tap on flat ground, {} sim ticks: best offset {} ticks, max error {:.3f}, avg {:.3f} units (press sent @{}, GD applied @{})",
                        s_run.simPath.size(), bestOffset, bestMax, bestAvg, s_run.accPressTick, s_lastRealPush);
                    s_run.stage = Stage::Sweep;
                    s_diagTicks = 0;
                    s_run.group = 0;
                    s_run.pressed = false;
                    setHack("cbf", false);
                }
                break;
        }
    }

    // Replay-CBF test.
    // Pass 1 (CBF off): press 40 units before each of the first kSnapGroups
    // spike sets (that survives) and save those clicks as a macro.
    // Pass 2 (after a restart, CBF on and following that macro): press
    // earlier by kExtra units and check the click is moved onto the macro's tick.
    constexpr int kSnapGroups = 7;
    std::vector<int> const kExtra = {0, 3, 6, 9, 12, 20, -5};
    std::vector<cmm::replay::Input> s_recorded;
    int s_pass = 1;

    void writeMacro(PlayLayer* pl) {
        std::error_code ec;
        auto dir = saveDir() / "macros";
        std::filesystem::create_directories(dir, ec);
        std::ofstream file(dir / "cmm-selftest.txt");
        file << "Cat's Mod Menu macro v1\nlevel=CMM Self Test\nid=0\n";
        for (auto const& in : s_recorded) file << in.tick << " " << (in.down ? 1 : 0) << " 1 0\n";
    }

    void sweepTick(PlayLayer* pl, int64_t now) {
        auto real = pl->m_player1;
        if (s_run.group >= kSnapGroups) {
            if (s_pass == 1) {
                writeMacro(pl);
                setHack("cbf", true);
                s_replayTouched = true;
                cmm::cbf::setReplay("cmm-selftest");
                setHack("prediction", true);
                setHack("framecounter", true);
                s_oldHide10 = cmm::val<bool>("framecounter.hide10", true);
                cmm::val<bool>("framecounter.hide10", true) = false;  // our windows are 11 ticks
                log::info("SELFTEST pass 1 recorded {} clicks, restarting for pass 2 with CBF following them", s_recorded.size());
                s_pass = 2;
                s_run.group = 0;
                s_run.pressed = false;
                pl->resetLevel();
                s_diagTicks = 0;
                s_diagUpdates = 0;
                return;
            }
            setHack("cbf", false);
            setHack("prediction", false);
            setHack("framecounter", false);
            setHack("noclip", false);
            setHack("hitboxesondeath", true);
            setHack("showhitboxes", false);
            s_run.stage = Stage::DeathHitboxes;
            s_run.wait = 0;
            log::info("SELFTEST noclip off, waiting to die for hitboxes-on-death");
            return;
        }
        int extra = s_pass == 2 ? kExtra[s_run.group % kExtra.size()] : 0;
        int dist = 40 + extra;
        float spikeX = kFirstGroup + kGroupGap * s_run.group;
        float x = real->getPositionX();

        if (!s_run.pressed) {
            if (x < spikeX - dist) return;
            s_run.deathsBefore = cmm::stats::noclipDeaths;
            s_run.savedBefore = cmm::cbf::savedThisSession();
            s_run.sentAt = now;
            log::info("SELFTEST diag press pass {} group {}: progress {} x {:.2f} playerUpdates {}", s_pass, s_run.group,
                pl->m_gameState.m_currentProgress, real->getPositionX(), s_diagUpdates);
            if (s_pass == 1) s_recorded.push_back({pl->m_gameState.m_currentProgress, true, 1, false});
            pl->handleButton(true, 1, true);
            s_run.pressed = true;
            s_run.released = false;
            return;
        }
        if (!s_run.released) {
            if (s_pass == 1) s_recorded.push_back({pl->m_gameState.m_currentProgress, false, 1, false});
            pl->handleButton(false, 1, true);
            s_run.released = true;
            s_run.shot = false;
            return;
        }
        // The frame window ring of this press is right behind the player here.
        if (s_pass == 2 && s_run.group == 3 && !s_run.shot && x >= spikeX + 30.f) {
            s_run.shot = true;
            screenshot("selftest-framewindows.png");
        }
        if (x < spikeX + 160.f) return;

        int died = cmm::stats::noclipDeaths - s_run.deathsBefore;
        int saved = cmm::cbf::savedThisSession() - s_run.savedBefore;
        if (s_pass == 1) {
            log::info("SELFTEST pass 1: press 40 units early -> {}", died ? "DIED" : "survived");
        } else {
            auto const& d = cmm::cbf::lastDecision();
            log::info("SELFTEST pass 2: press {} units early ({:+} vs replay) -> {} | saved={} (last click: delay {}, your tick {}, replay tick {})",
                dist, extra, died ? "DIED" : "survived", saved, d.delay, d.yourTick, d.replayTick);
        }
        s_run.group++;
        s_run.pressed = false;
    }

    void onTick(PlayLayer* pl, int64_t now) {
        switch (s_run.stage) {
            case Stage::Hitboxes:
                s_run.wait++;
                if (s_run.wait == 60) {
                    auto node = pl->m_debugDrawNode;
                    log::info("SELFTEST hitboxes on: node visible={} vertices={} opacity={} z={} parentVisible={} shouldDraw={}",
                        node && node->isVisible(), node ? node->m_nBufferCount : -1, node ? static_cast<int>(node->getOpacity()) : -1,
                        node ? node->getZOrder() : 0, node && node->getParent() && node->getParent()->isVisible(), pl->shouldDrawDebugHitboxes());
                    if (node) {
                        auto parent = node->getParent();
                        auto world = node->convertToWorldSpace({0, 0});
                        log::info("SELFTEST drawnode parent={} id='{}' world=({:.1f},{:.1f}) scale={:.2f} blend={}/{}",
                            parent ? typeid(*parent).name() : "none", parent ? parent->getID() : "",
                            world.x, world.y, node->getScale(), node->getBlendFunc().src, node->getBlendFunc().dst);
                        int visibleVerts = 0;
                        for (int i = 0; i < node->m_nBufferCount; i++) visibleVerts += node->m_pBuffer[i].colors.a > 0;
                        log::info("SELFTEST drawnode: {} of {} vertices have alpha > 0; parent is object layer: {}",
                            visibleVerts, node->m_nBufferCount, parent == pl->m_objectLayer);
                        for (int i = 0; i < std::min(node->m_nBufferCount, 6); i++) {
                            auto const& v = node->m_pBuffer[i];
                            log::info("SELFTEST vertex {}: ({:.1f},{:.1f}) rgba({},{},{},{})", i, v.vertices.x, v.vertices.y,
                                v.colors.r, v.colors.g, v.colors.b, v.colors.a);
                        }
                    }
                }
                if (s_run.wait == 62) {
                    screenshot("selftest-hitboxes.png");
                }
                if (s_run.wait == 70) {
                    setHack("showhitboxes", false);
                    s_run.stage = Stage::Sweep;
                    s_run.group = 0;
                    s_run.pressed = false;
                    s_pass = 1;
                    s_recorded.clear();
                }
                break;
            case Stage::Accuracy:
                accuracyTick(pl, now);
                break;
            case Stage::Sweep:
                sweepTick(pl, now);
                break;
            default:
                break;
        }
    }
}

class $modify(SelfTestPlayer, PlayerObject) {
    void update(float dt) {
        if (s_active && !cmm::sim::simulating()) {
            auto pl = PlayLayer::get();
            if (pl && this == pl->m_player1) s_diagUpdates++;
        }
        PlayerObject::update(dt);
    }

    bool pushButton(PlayerButton button) {
        if (s_active && !cmm::sim::simulating()) {
            auto pl = PlayLayer::get();
            if (pl && this == pl->m_player1) {
                s_lastRealPush = cmm::cbf::currentTick();
                if (s_run.stage == Stage::Sweep && s_run.pressed && s_run.pushAt < 0) s_run.pushAt = s_lastRealPush;
            }
        }
        return PlayerObject::pushButton(button);
    }
};

// Record the real player once per physics tick (a frame can hold several).
class $modify(SelfTestGameLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        // Diagnostics: GD's tick counter vs the player's X at the start of each pass.
        if (s_active && s_diagTicks < 40 && PlayLayer::get() == static_cast<GJBaseGameLayer*>(this)) {
            if (s_diagTicks % 4 == 0 || s_diagTicks < 4) {
                log::info("SELFTEST diag pass {} tick {}: progress {} x {:.2f} playerUpdates {} dt {:.5f}", s_pass, s_diagTicks,
                    m_gameState.m_currentProgress, m_player1->getPositionX(), s_diagUpdates, dt);
            }
            s_diagTicks++;
        }
        if (s_active && s_run.stage == Stage::Accuracy && s_run.accPhase == 1 && PlayLayer::get() == static_cast<GJBaseGameLayer*>(this)
            && s_run.realPath.size() < 145) {
            s_run.realPath.push_back(m_player1->getPosition());
        }
    }
};

class $modify(SelfTestPlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!s_active || PlayLayer::get() != this) return;
        if (m_level != s_level) {
            log::info("SELFTEST stopped: left the test level");
            finish();
            return;
        }

        if (s_run.stage == Stage::DeathHitboxes) {
            if (m_playerDied) {
                if (++s_run.deathFrames == 5) {
                    auto node = m_debugDrawNode;
                    log::info("SELFTEST hitboxes after death: visible={} vertices={}", node && node->isVisible(), node ? node->m_nBufferCount : -1);
                    screenshot("selftest-death.png");
                    finish();
                }
            } else if (++s_run.wait > 240 * 30) {
                log::info("SELFTEST never died");
                finish();
            }
            return;
        }

        int64_t now = cmm::cbf::currentTick();
        if (now == s_run.lastTick) return;
        s_run.lastTick = now;
        onTick(this, now);
    }
};


// ---- test 2: Smart StartPos + hitbox types (runs first, then the CBF test) ----
namespace {
    bool s_typesActive = false;
    bool s_typesOnly = false;  // "selftest-hitboxes.txt": only the hitbox/startpos level
    GJGameLevel* s_typesLevel = nullptr;
    int s_typesTicks = 0;
    int64_t s_typesLast = -1;

    void startMain() {
        s_diagTicks = 0;
        s_diagUpdates = 0;
        // Flat ground, then sets of three spikes, end far away.
        std::string raw = "kA2,0,kA4,0;";
        for (int g = 0; g < kGroups; g++) {
            int x = static_cast<int>(kFirstGroup + kGroupGap * g);
            for (int i = 0; i < 3; i++) raw += fmt::format("1,8,2,{},3,15;", x + 30 * i);
        }
        raw += "1,1,2,30000,3,15;";

        auto level = GJGameLevel::create();
        level->retain();
        s_level = level;
        level->m_levelName = "CMM Self Test";
        level->m_levelString = ZipUtils::compressString(raw, false, 0, 0);
        level->m_levelType = GJLevelType::Editor;

        s_oldStage = cmm::val<int>("cbf.stage", 3);
        s_oldLook = cmm::val<int>("cbf.lookahead", 120);
        s_oldLogfile = cmm::val<bool>("cbf.logfile", true);
        cmm::val<int>("cbf.stage", 3) = 3;
        cmm::val<int>("cbf.lookahead", 120) = 120;
        cmm::val<bool>("cbf.logfile", true) = false;
        setHack("noclip", true);
        setHack("cbf", false);
        setHack("prediction", false);
        setHack("framecounter", false);
        setHack("showhitboxes", true);
        setHack("hitboxesondeath", false);
        setHack("hitboxtrail", false);
        setHack("startposswitcher", false);
        setHack("bot.record", false);
        setHack("bot.play", false);
        s_run = {};
        s_active = true;
        log::info("SELFTEST start");
        CCDirector::get()->replaceScene(PlayLayer::scene(level, false));
    }

    void startTypesTest() {
        // Before the start position: ship, 2x speed, mini, flipped gravity.
        // The start position itself is left at default (cube, 1x).
        std::string raw = "kA2,0,kA4,0;";
        raw += "1,13,2,150,3,45;1,202,2,210,3,45;1,101,2,270,3,45;1,11,2,330,3,45;";
        raw += "1,31,2,450,3,45;";
        // After it, one of each kind of object to look at the hitboxes.
        raw += "1,1,2,600,3,15;1,8,2,660,3,15;1,36,2,720,3,75;1,35,2,780,3,3;1,88,2,840,3,105;";
        raw += "1,1329,2,900,3,75;1,289,2,960,3,15;1,10,2,1020,3,45;1,901,2,1080,3,15;1,1,2,1140,3,165;";
        // Two tall walls: the first with All Passable off, the second with it on.
        for (int y = 15; y <= 795; y += 30) raw += fmt::format("1,1,2,1300,3,{};1,1,2,1800,3,{};", y, y);
        raw += "1,1,2,30000,3,15;";

        auto level = GJGameLevel::create();
        level->retain();
        s_typesLevel = level;
        level->m_levelName = "CMM Self Test 2";
        level->m_levelString = ZipUtils::compressString(raw, false, 0, 0);
        level->m_levelType = GJLevelType::Editor;
        setHack("noclip", true);
        setHack("smartstartpos", true);
        setHack("cointracers", true);
        setHack("norotation", true);
        setHack("classicpercentage", true);
        setHack("bestinpercentage", true);
        setHack("label.levelinfo", true);
        setHack("label.custom", true);
        setHack("trajectory", true);
        setHack("nocircles", true);
        setHack("label.keys", true);
        setHack("label.bestpercent", true);
        setHack("label.totalattempts", true);
        setHack("label.objects", true);
        setHack("label.sessiondeaths", true);
        setHack("label.speed", true);
        setHack("hitboxmultiplier", true);
        s_oldHbPlayer = cmm::val<float>("hitboxmultiplier.player", 1.f);
        cmm::val<float>("hitboxmultiplier.player", 1.f) = 3.f;
        setHack("hideattempts", true);
        setHack("allpassable", false);
        setHack("label.practice", true);
        setHack("label.gamemode", true);
        setHack("label.position", true);
        setHack("label.jumps", true);
        setHack("showhitboxes", true);
        setHack("hitboxtrail", true);
        setHack("cbf", false);
        setHack("startposswitcher", true);
        s_typesActive = true;
        s_typesTicks = 0;
        log::info("SELFTEST2 start");
        CCDirector::get()->replaceScene(PlayLayer::scene(level, false));
    }
}

class $modify(SelfTestTypesLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!s_typesActive || PlayLayer::get() != this) return;
        if (m_level != s_typesLevel) {
            log::info("SELFTEST2 stopped: left the test level");
            s_typesActive = false;
            restoreSettings();
            return;
        }
        int64_t now = cmm::cbf::currentTick();
        if (now == s_typesLast) return;
        s_typesLast = now;
        s_typesTicks++;
        auto p = m_player1;
        if (s_typesTicks == 5) {
            log::info("SELFTEST2 start state: ship={} mini={} speed={:.2f} upsideDown={} x={:.0f} (want ship, mini, upside down, 2x speed)",
                p->m_isShip, p->m_vehicleSize < 1.f, p->m_playerSpeed, p->m_isUpsideDown, p->getPositionX());
        }
        if (s_typesTicks == 25 && m_objects) {
            // What scale does GD's object opacity use (No Object Fade returns 1)?
            for (auto o : CCArrayExt<GameObject*>(m_objects)) {
                if (o && o->m_objectID == 1 && o->getParent()) {
                    setHack("noobjectfade", false);
                    float raw = this->opacityForObject(o);
                    setHack("noobjectfade", true);
                    float forced = this->opacityForObject(o);
                    setHack("noobjectfade", false);
                    log::info("SELFTEST2 opacityForObject(block): normal {:.3f}, with No Object Fade {:.3f}", raw, forced);
                    break;
                }
            }
        }
        if (s_typesTicks == 30) {
            auto n1 = m_debugDrawNode;
            auto n2 = m_debugDrawNode2;
            log::info("SELFTEST2 hitbox nodes: node1 vertices={} node2 {} vertices={} visible={}",
                n1 ? n1->m_nBufferCount : -1, n2 ? "exists" : "missing", n2 ? n2->m_nBufferCount : -1, n2 && n2->isVisible());
            screenshot("selftest-types.png");
        }
        if (s_typesTicks == 200) screenshot("selftest-types2.png");
        // Walls at x 1300 (All Passable off) and 1800 (on).
        static int deathsBeforeWalls = 0, deathsAfterFirst = 0;
        static bool passedFirst = false, passedSecond = false;
        if (s_typesTicks == 1) { passedFirst = passedSecond = false; deathsBeforeWalls = cmm::stats::noclipDeaths; }
        if (!passedFirst && p->getPositionX() > 1360.f) {
            passedFirst = true;
            deathsAfterFirst = cmm::stats::noclipDeaths;
            setHack("allpassable", true);
        }
        if (!passedSecond && p->getPositionX() > 1860.f) {
            passedSecond = true;
            log::info("SELFTEST2 walls: noclip hits at wall 1 (All Passable off) = {}, at wall 2 (on) = {}",
                deathsAfterFirst - deathsBeforeWalls, cmm::stats::noclipDeaths - deathsAfterFirst);
        }
        if (s_typesTicks == 1200 || (passedSecond && s_typesTicks > 220)) {
            s_typesActive = false;
            log::info("SELFTEST2 DONE, starting CBF test");
            if (s_typesOnly) {
                restoreSettings();
                log::info("SELFTEST DONE");
            } else {
                // Put everything this level changed back (All Passable, hitbox
                // multiplier...) before the CBF test.
                restoreSettings();
                Loader::get()->queueInMainThread([] { startMain(); });
            }
        }
    }
};

class $modify(SelfTestMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        std::error_code ec;
        auto trigger = saveDir() / "selftest.txt";
        auto triggerTypes = saveDir() / "selftest-hitboxes.txt";
        if (!s_started && std::filesystem::exists(triggerTypes, ec)) {
            s_typesOnly = true;
            trigger = triggerTypes;
        }
        if (!s_started && std::filesystem::exists(trigger, ec)) {
            s_started = true;
            std::filesystem::remove(trigger, ec);
            this->scheduleOnce(schedule_selector(SelfTestMenu::startSelfTest), 1.f);
        }
        return true;
    }

    void startSelfTest(float) {
        s_oldReplay = cmm::cbf::replayName();
        startTypesTest();
    }
};
