// Developer test of No Orb Pulse. Only runs if "selftest-orb.txt" exists in the
// mod's save folder (deleted when it starts). Plays main level 20 with noclip
// and logs ("ORBTEST") how much the music-scaled objects on screen change size,
// with the hack off and on at a few sizes.
#include <core/Hacks.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;

namespace {
    bool s_active = false;
    int s_phase = 0, s_frames = 0;
    float s_min = 1e9f, s_max = -1e9f;
    int s_seen = 0;
    struct Range { float lo = 1e9f, hi = -1e9f, clo = 1e9f, chi = -1e9f; int n = 0; };
    std::unordered_map<GameObject*, Range> s_objs;
    GJGameLevel* s_level = nullptr;
    bool s_oldOn = false;
    float s_oldSize = 0.5f;
    struct Phase { char const* name; bool on; float size; };
    constexpr Phase kPhases[] = {{"off", false, 0.f}, {"on, size 0", true, 0.f}, {"on, size 0.75", true, 0.75f}, {"on, size 1", true, 1.f}};

    std::filesystem::path saveDir() { return std::filesystem::path(Mod::get()->getSaveDir().native()); }

    void apply() {
        if (auto h = cmm::find("noorbpulse")) h->enabled = kPhases[s_phase].on;
        cmm::val<float>("noorbpulse.size", 0.75f) = kPhases[s_phase].size;
        s_min = 1e9f; s_max = -1e9f; s_seen = 0; s_frames = 0;
        s_objs.clear();
    }
}

class $modify(OrbTestLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!s_active || PlayLayer::get() != this) return;
        if (!m_level || std::string(m_level->m_levelName) != std::string(s_level->m_levelName)) { s_active = false; return; }
        if (m_gameState.m_currentProgress == 0) return;
        s_frames++;
        auto win = CCDirector::get()->getWinSize();
        for (auto obj : CCArrayExt<GameObject*>(m_objects)) {
            if (!obj || !obj->getParent() || !obj->isVisible()) continue;
            static std::unordered_set<int> const kOrbs = {36, 84, 141, 1022, 1330, 1333, 1594, 1704, 1751, 3004, 35, 67, 140, 1332, 3005};
            if (!kOrbs.contains(obj->m_objectID)) continue;
            auto screen = obj->convertToWorldSpace({0, 0});
            if (screen.x < 0 || screen.x > win.width) continue;
            float sc = obj->getScale();
            s_seen++;
            auto& r = s_objs[obj];
            static GameObject* s_traced = nullptr;
            static int s_traceN = 0;
            static std::string s_trace;
            if (obj->m_objectID == 36 && s_traced == nullptr) { s_traced = obj; s_traceN = 0; s_trace.clear(); }
            if (obj == s_traced && s_traceN++ % 8 == 0) {
                s_trace += fmt::format(" {:.2f}", sc);
                if (s_traceN > 400) {
                    log::info("ORBTEST trace ({}): screen x {:.0f}:{}", kPhases[s_phase].name, screen.x, s_trace);
                    s_traced = reinterpret_cast<GameObject*>(1);
                }
            }
            if (s_frames == 1) s_traced = nullptr;
            r.lo = std::min(r.lo, sc); r.hi = std::max(r.hi, sc); r.n++;
            // Biggest scale among its child sprites (any depth).
            float cs = 0.f;
            std::function<void(CCNode*)> walk = [&](CCNode* n) {
                if (auto kids = n->getChildren()) for (auto k : CCArrayExt<CCNode*>(kids)) { cs = std::max(cs, k->getScale()); walk(k); }
            };
            walk(obj);
            r.clo = std::min(r.clo, cs); r.chi = std::max(r.chi, cs);
        }
        if (s_frames == 300) {
            int par = 0, vis = 0, ons = 0;
            for (auto obj : CCArrayExt<GameObject*>(m_objects)) {
                if (!obj) continue;
                par += obj->getParent() != nullptr;
                vis += obj->isVisible();
                auto sp = obj->convertToWorldSpace({0, 0});
                if (obj->getParent() && sp.x >= 0 && sp.x <= win.width) { ons++; if (ons == 1) log::info("ORBTEST first on screen: id {} at world ({:.0f},{:.0f}) screen ({:.0f},{:.0f}) scale {:.3f} visible {} opacity {}", obj->m_objectID, obj->getPositionX(), obj->getPositionY(), sp.x, sp.y, obj->getScale(), obj->isVisible(), obj->getOpacity()); }
            }
            log::info("ORBTEST diag: {} with parent, {} visible, {} with parent on screen; win {:.0f}", par, vis, ons, win.width);
        }
        if (s_frames == 10) log::info("ORBTEST {} objects in the level, player at x {:.0f}", m_objects ? m_objects->count() : 0u, m_player1->getPositionX());
        // Pictures of the orbs: three frames 12 apart, while one is on screen.
        static int s_shots = 0, s_shotFrame = 0;
        if (s_frames == 1) { s_shots = 0; s_shotFrame = 0; }
        if (s_seen > 0 && s_shots < 3 && s_frames > 240 && (s_shotFrame == 0 || s_frames - s_shotFrame >= 12)) {
            s_shotFrame = s_frames;
            auto size = CCDirector::get()->getWinSize();
            auto rt = CCRenderTexture::create(static_cast<int>(size.width), static_cast<int>(size.height));
            rt->begin();
            CCDirector::get()->getRunningScene()->visit();
            rt->end();
            if (auto img = rt->newCCImage(true)) {
                auto file = fmt::format("orbtest-{}-{}.png", s_phase, s_shots);
                img->saveToFile((saveDir() / file).string().c_str(), false);
                img->release();
            }
            s_shots++;
        }
        if (s_frames < 240 * 6) return;
        double spread = 0, cspread = 0, avgSize = 0;
        int counted = 0;
        for (auto& [o, r] : s_objs) {
            if (r.n < 60) continue;
            spread += r.hi - r.lo; cspread += r.chi - r.clo; avgSize += (r.hi + r.lo) / 2; counted++;
            if (counted <= 2) log::info("ORBTEST   object {} (type {}): scale {:.3f}-{:.3f}, children {:.3f}-{:.3f}", o->m_objectID, static_cast<int>(o->m_objectType), r.lo, r.hi, r.clo, r.chi);
        }
        log::info("ORBTEST {}: {} objects watched; own scale changes by {:.4f} on average (size {:.3f}), first child's by {:.4f}", kPhases[s_phase].name,
            counted, counted ? spread / counted : 0.0, counted ? avgSize / counted : 0.0, counted ? cspread / counted : 0.0);
        if (++s_phase < static_cast<int>(std::size(kPhases))) return apply();
        if (auto h = cmm::find("noorbpulse")) h->enabled = s_oldOn;
        cmm::val<float>("noorbpulse.size", 0.75f) = s_oldSize;
        s_active = false;
        log::info("ORBTEST DONE");
        this->onQuit();
    }
};

class $modify(OrbTestMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        std::error_code ec;
        auto trigger = saveDir() / "selftest-orb.txt";
        if (s_active || !std::filesystem::exists(trigger, ec)) return true;
        // A saved level by name (the trigger file's text), else Polargeist.
        std::string name;
        { std::ifstream in(trigger); std::getline(in, name); }
        GJGameLevel* level = nullptr;
        if (!name.empty()) {
            if (auto levels = GameLevelManager::get()->getSavedLevels(false, 0)) {
                for (auto l : CCArrayExt<GJGameLevel*>(levels)) if (l && std::string(l->m_levelName) == name) level = l;
            }
        }
        if (!level) level = GameLevelManager::get()->getMainLevel(3, false);  // Polargeist: orbs and pads
        if (!level) return true;
        log::info("ORBTEST level {}", std::string(level->m_levelName));
        std::filesystem::remove(trigger, ec);
        level->retain();
        s_level = level;
        if (auto h = cmm::find("noorbpulse")) s_oldOn = h->enabled;
        s_oldSize = cmm::val<float>("noorbpulse.size", 0.75f);
        if (auto h = cmm::find("noclip")) h->enabled = true;
        s_active = true;
        s_phase = 0;
        apply();
        Loader::get()->queueInMainThread([] { CCDirector::get()->replaceScene(PlayLayer::scene(s_level, false)); });
        return true;
    }
};
