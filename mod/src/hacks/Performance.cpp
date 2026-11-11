// Performance tab: one-click "max performance" plus a few hacks that save
// GPU/CPU without changing gameplay.
#include <core/Hacks.hpp>
#include <core/Log.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/PlayLayer.hpp>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

using namespace geode::prelude;

namespace cmm::global { void applyFps(); }

namespace {
    // Visual-only hacks that make GD cheaper to draw. All allowed on Pointercrate.
    constexpr char const* kMaxPerf[] = {
        "noparticles", "noglow", "noshaders", "noportallightning", "autoldm",
        "novehicleparticles", "hidecompletevfx", "noendshake", "perf.unfocused",
    };

    bool s_unfocusedSlow = false;
    float s_focusTimer = 0.f;

    bool gameFocused() {
#ifdef GEODE_IS_WINDOWS
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        return pid == GetCurrentProcessId();
#else
        return true;
#endif
    }

    void restoreFps() {
        if (cmm::on("fpsbypass")) cmm::global::applyFps();
        else GameManager::get()->updateCustomFPS();
    }
}

class $modify(PerfScheduler, CCScheduler) {
    void update(float dt) {
        CCScheduler::update(dt);

        // Background FPS limit: check focus twice a second.
        s_focusTimer -= dt;
        if (s_focusTimer > 0.f) return;
        s_focusTimer = 0.5f;

        bool slow = cmm::on("perf.unfocused") && !gameFocused();
        if (slow == s_unfocusedSlow) return;
        s_unfocusedSlow = slow;
        if (slow) CCApplication::sharedApplication()->setAnimationInterval(1.0 / std::max(5, cmm::val<int>("perf.unfocused.fps", 30)));
        else restoreFps();
    }
};

class $modify(PerfPlayLayer, PlayLayer) {
    void onQuit() {
        PlayLayer::onQuit();
        if (cmm::on("perf.cleancache")) {
            CCTextureCache::sharedTextureCache()->removeUnusedTextures();
            cmm::log::info("Freed unused textures");
        }
    }
};

namespace cmm {
    void registerPerformance() {
        using enum Tab;
        constexpr auto OK = PC::Allowed;

        Hack maxPerf{"perf.max", "Max Performance", "Turns on every visual-only hack that makes GD faster to draw (particles, glow, shaders, LDM...).", Performance, OK};
        maxPerf.action = [] {
            for (auto id : kMaxPerf) {
                if (auto h = find(id)) setEnabled(*h, true);
            }
        };
        add(std::move(maxPerf));

        Hack restore{"perf.restore", "Undo Max Performance", "Turns those hacks back off.", Performance, OK};
        restore.action = [] {
            for (auto id : kMaxPerf) {
                if (auto h = find(id)) setEnabled(*h, false);
            }
        };
        add(std::move(restore));

        add({"perf.unfocused", "Background FPS Limit", "Lowers the frame rate while GD isn't the active window, so it doesn't use your GPU for nothing.", Performance, OK,
            false, [] { optInt("FPS", "perf.unfocused.fps", 30, 5, 120); }});
        add({"perf.cleancache", "Free Textures on Exit", "Frees unused textures when you leave a level (less memory use).", Performance, OK});
    }
}
