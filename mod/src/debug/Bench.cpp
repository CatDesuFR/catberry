// Developer FPS benchmark. Only runs if "selftest-bench.txt" exists in the
// mod's save folder (deleted as soon as it starts). The file may hold a main
// level ID (default 20). Plays that level with noclip three times and writes
// "BENCH" lines to the Geode log:
//   1. your settings exactly
//   2. your settings with VSync off
//   3. every cbmm feature off (FPS Bypass kept, VSync off)
// Your settings are put back afterwards; nothing is saved.
#include <core/Hacks.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace geode::prelude;

namespace cmm::global { void applyFps(); }

namespace {
    using Clock = std::chrono::steady_clock;

    bool s_active = false;
    int s_phase = 0;
    Clock::time_point s_phaseStart, s_lastFrame;
    std::vector<float> s_frames;  // frame times in ms, after the warm-up
    // Work per frame: from the start of drawScene (game update + mods + drawing)
    // until the frame is handed to the driver, so a frame cap doesn't hide it.
    std::vector<float> s_work;
    Clock::time_point s_drawStart;
    bool s_inDraw = false;
    std::vector<std::pair<std::string, bool>> s_saved;
    GJGameLevel* s_level = nullptr;

    constexpr double kWarmup = 3.0, kMeasure = 15.0;
    char const* const kPhases[] = {"your settings", "your settings, VSync off", "cbmm features off"};

    std::filesystem::path saveDir() { return std::filesystem::path(Mod::get()->getSaveDir().native()); }

    void setHack(std::string_view id, bool on) {
        if (auto h = cmm::find(id); h && h->enabled != on) cmm::setEnabled(*h, on);
    }

    void restore() {
        for (auto& [id, on] : s_saved) setHack(id, on);
        s_saved.clear();
        cmm::global::applyFps();
        s_active = false;
    }

    void startPhase(PlayLayer* pl) {
        if (s_phase == 1) setHack("vsync", false);
        if (s_phase == 2) {
            for (auto& h : cmm::all()) {
                if (h.id != "fpsbypass" && h.id != "noclip" && h.enabled) cmm::setEnabled(h, false);
            }
            setHack("noclip", true);
            cmm::global::applyFps();
        }
        s_frames.clear();
        s_work.clear();
        s_phaseStart = s_lastFrame = Clock::now();
        if (pl) pl->resetLevel();
    }

    void report() {
        if (s_frames.empty()) return;
        double total = 0;
        for (float f : s_frames) total += f;
        auto sorted = s_frames;
        std::sort(sorted.begin(), sorted.end(), std::greater<>());
        size_t worst = std::max<size_t>(1, sorted.size() / 100);
        double worstTotal = 0;
        for (size_t i = 0; i < worst; i++) worstTotal += sorted[i];
        log::info("BENCH {}: {:.0f} FPS average, {:.0f} FPS 1% low, {:.2f} ms average frame, {:.2f} ms worst ({} frames)",
            kPhases[s_phase], s_frames.size() * 1000.0 / total, worst * 1000.0 / worstTotal,
            total / s_frames.size(), sorted.front(), s_frames.size());
        if (s_work.empty()) return;
        double work = 0;
        for (float w : s_work) work += w;
        auto ws = s_work;
        std::sort(ws.begin(), ws.end(), std::greater<>());
        double wworst = 0;
        size_t wn = std::max<size_t>(1, ws.size() / 100);
        for (size_t i = 0; i < wn; i++) wworst += ws[i];
        log::info("BENCH {} work per frame: {:.3f} ms average, {:.3f} ms worst 1% (could run at ~{:.0f} FPS)",
            kPhases[s_phase], work / s_work.size(), wworst / wn, 1000.0 * s_work.size() / work);
    }
}

class $modify(BenchDirector, CCDirector) {
    void drawScene() {
        bool timing = s_active && std::chrono::duration<double>(Clock::now() - s_phaseStart).count() > kWarmup;
        s_inDraw = timing;
        if (timing) s_drawStart = Clock::now();
        CCDirector::drawScene();
        s_inDraw = false;
    }
};

class $modify(BenchView, CCEGLView) {
    void swapBuffers() {
        if (s_inDraw) {
            s_work.push_back(std::chrono::duration<float, std::milli>(Clock::now() - s_drawStart).count());
            s_inDraw = false;
        }
        CCEGLView::swapBuffers();
    }
};

class $modify(BenchPlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!s_active || PlayLayer::get() != this) return;
        if (m_level != s_level) { restore(); log::info("BENCH stopped (left the level)"); return; }
        auto now = Clock::now();
        double since = std::chrono::duration<double>(now - s_phaseStart).count();
        if (since > kWarmup) s_frames.push_back(std::chrono::duration<float, std::milli>(now - s_lastFrame).count());
        s_lastFrame = now;
        if (since < kWarmup + kMeasure) return;
        report();
        if (++s_phase < static_cast<int>(std::size(kPhases))) return startPhase(this);
        restore();
        log::info("BENCH DONE");
        this->onQuit();
    }
};

class $modify(BenchMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        std::error_code ec;
        auto trigger = saveDir() / "selftest-bench.txt";
        if (s_active || !std::filesystem::exists(trigger, ec)) return true;
        int id = 20;
        {
            std::ifstream in(trigger);
            int v = 0;
            if (in >> v && v > 0) id = v;
        }
        std::filesystem::remove(trigger, ec);
        auto level = GameLevelManager::get()->getMainLevel(id, false);
        if (!level) { log::error("BENCH: no main level {}", id); return true; }
        level->retain();
        s_level = level;
        for (auto& h : cmm::all()) s_saved.push_back({h.id, h.enabled});
        setHack("noclip", true);
        s_active = true;
        s_phase = 0;
        auto renderer = reinterpret_cast<char const*>(glGetString(GL_RENDERER));
        log::info("BENCH start: level {} ({}), GPU {}, FPS Bypass {} ({}), VSync {}", id, std::string(level->m_levelName),
            renderer ? renderer : "?", cmm::on("fpsbypass") ? "on" : "off", cmm::val<float>("fpsbypass.value", 240.f), cmm::on("vsync") ? "on" : "off");
        Loader::get()->queueInMainThread([] {
            CCDirector::get()->replaceScene(PlayLayer::scene(s_level, false));
            s_phaseStart = s_lastFrame = Clock::now();
        });
        return true;
    }
};
