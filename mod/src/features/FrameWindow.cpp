// Frame Window analyser and counter. See FrameWindow.hpp.
//
// Timing (measured in game, see ClickBeforeFrames.cpp): a click that came in
// at progress P (between frames, after tick T) acts on tick T + 2. From the
// player's state after tick "now", run tick k of the hidden player is real
// tick now + 1 + k, so that click acts on run tick (T - now) + 1.
#include "FrameWindow.hpp"
#include "ClickBeforeFrames.hpp"
#include "Sim.hpp"
#include "WorldOverlay.hpp"
#include <core/Hacks.hpp>
#include <core/Log.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace cmm::fw {
    namespace {
        struct Result {
            bool analysed = false;
            int window = 0;     // ticks in a row that survive (0 = not even the original)
            int earliest = 0;   // offset of the earliest surviving tick (<= 0)
            int latest = 0;
            bool passed = false;   // the player reached this click
            CCPoint where;
        };

        std::vector<Result> s_results;
        size_t s_replaySize = 0;

        bool analyserOn() { return cmm::on("prediction") || cmm::on("framecounter"); }
        int searchRange() { return std::clamp(cmm::val<int>("prediction.range", 16), 2, 64); }

        // NaN GD's colour bands.
        ccColor3B bandColor(int window) {
            if (window <= 1) return {255, 70, 70};
            if (window == 2) return {255, 153, 51};
            if (window == 3) return {255, 217, 70};
            if (window == 4) return {255, 255, 255};
            if (window <= 6) return {102, 222, 135};
            if (window <= 8) return {102, 171, 255};
            return {92, 107, 237};
        }
        int bandIndex(int window) {
            if (window <= 1) return 0;
            if (window <= 4) return window - 1;
            if (window <= 6) return 4;
            if (window <= 8) return 5;
            return 6;  // 9-10 (and over 10 when those are shown)
        }
        char const* kBandNames[] = {"1", "2", "3", "4", "5-6", "7-8", "9-10"};

        bool isP1(replay::Input const& in) { return in.button == 1 && !in.player2; }

        // NaN GD shows no marker for windows over 10 (easy clicks); optional.
        bool shown(int window) { return window <= 10 || !cmm::val<bool>("framecounter.hide10", true); }

        // Runs the hidden player from the real one with the replay's clicks,
        // click `index` (and its release) moved by `offset` ticks. True if it
        // survives to `horizon`.
        bool leg(PlayLayer* pl, std::vector<replay::Input> const& inputs, size_t index, size_t pairIndex,
                 int offset, unsigned now, unsigned ppt, int horizon) {
            auto p = sim::scratch(pl, 2);
            if (!p) return false;
            sim::copyState(p, pl->m_player1);
            sim::clearStopped();

            // Run tick of every P1 click still ahead.
            struct Event { int k; bool down; };
            std::vector<Event> events;
            for (size_t j = 0; j < inputs.size(); j++) {
                auto const& in = inputs[j];
                if (!isP1(in) || in.tick <= now) continue;
                int k = static_cast<int>((in.tick - now) / ppt) + 1;
                if (j == index || j == pairIndex) k += offset;
                if (k > horizon) continue;
                events.push_back({std::max(0, k), in.down});
            }
            std::stable_sort(events.begin(), events.end(), [](auto const& a, auto const& b) { return a.k < b.k; });

            size_t e = 0;
            bool alive = true;
            for (int k = 0; k < horizon && alive; k++) {
                while (e < events.size() && events[e].k == k) {
                    if (events[e].down) p->pushButton(PlayerButton::Jump);
                    else p->releaseButton(PlayerButton::Jump);
                    e++;
                }
                alive = sim::step(pl, p);
                if (!alive && sim::stopped()) alive = true;  // can't see further: count as alive
                if (sim::stopped()) break;
            }
            sim::restoreTouched();
            return alive;
        }

        void analyse(PlayLayer* pl, std::vector<replay::Input> const& inputs, size_t index, unsigned now, unsigned ppt) {
            auto& r = s_results[index];
            r.analysed = true;
            auto const& in = inputs[index];
            int k = static_cast<int>((in.tick - now) / ppt) + 1;

            // A press moves together with its release (the hold stays the same).
            size_t pairIndex = SIZE_MAX, nextIndex = SIZE_MAX;
            for (size_t j = index + 1; j < inputs.size(); j++) {
                if (!isP1(inputs[j])) continue;
                if (in.down && !inputs[j].down && pairIndex == SIZE_MAX) { pairIndex = j; continue; }
                nextIndex = j;
                break;
            }
            // Survive until a few ticks before the next click (at least 12, at most 240 ticks).
            int horizon = k + 120;
            if (nextIndex != SIZE_MAX) horizon = static_cast<int>((inputs[nextIndex].tick - now) / ppt) + 1 - 4;
            horizon = std::clamp(horizon, k + 12, k + 240);

            int range = searchRange();
            if (!leg(pl, inputs, index, pairIndex, 0, now, ppt, horizon)) {
                r.window = 0;
                r.earliest = r.latest = 0;
                return;
            }
            int latest = 0, earliest = 0;
            while (latest < range && leg(pl, inputs, index, pairIndex, latest + 1, now, ppt, horizon + latest + 1)) latest++;
            int minOffset = -std::min(range, k);  // can't act before run tick 0
            while (earliest > minOffset && leg(pl, inputs, index, pairIndex, earliest - 1, now, ppt, horizon)) earliest--;
            r.earliest = earliest;
            r.latest = latest;
            r.window = latest - earliest + 1;
            log::debug("Frame window: click {} ({}) window {} (from {:+} to {:+} ticks)", index, in.down ? "press" : "release",
                r.window, earliest, latest);
        }
    }

    bool earliestFor(size_t replayIndex, unsigned& progress) {
        if (!cmm::on("prediction") || replayIndex >= s_results.size() || !s_results[replayIndex].analysed) return false;
        auto const& r = s_results[replayIndex];
        if (r.window == 0) return false;
        auto const& inputs = cbf::replayInputs();
        if (replayIndex >= inputs.size()) return false;
        long long p = static_cast<long long>(inputs[replayIndex].tick) + static_cast<long long>(r.earliest) * cbf::progressPerTick();
        progress = static_cast<unsigned>(std::max(0LL, p));
        return true;
    }

    void drawOverlay() {
        auto pl = PlayLayer::get();
        if (!pl || !cmm::on("framecounter")) return;
        int counts[7] = {};
        int total = 0;
        for (auto const& r : s_results) {
            if (!r.passed || !r.analysed || !shown(r.window)) continue;
            counts[bandIndex(r.window)]++;
            total++;
        }
        auto& io = ImGui::GetIO();
        ImGui::SetNextWindowPos({10.f, io.DisplaySize.y * 0.45f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.45f);
        auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("##framewindows", nullptr, flags)) {
            if (cbf::replayName().empty()) {
                ImGui::TextDisabled("Frame windows: pick a replay in the CBF window");
            } else {
                for (int b = 6; b >= 0; b--) {
                    int window = b <= 3 ? b + 1 : b == 4 ? 5 : b == 5 ? 7 : 9;
                    auto c = bandColor(window);
                    ImGui::TextColored({c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f}, "%s: %d", kBandNames[b], counts[b]);
                }
                ImGui::TextDisabled("%d clicks", total);
            }
        }
        ImGui::End();
    }
}

using namespace cmm::fw;

class $modify(FrameWindowLayer, PlayLayer) {
    struct Fields {
        Ref<CCDrawNode> rings;
        Ref<CCNode> numbers;
    };

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        auto f = m_fields.self();
        auto rings = CCDrawNode::create();
        rings->setID("cats-mod-menu-frame-windows");
        this->addChild(rings, 995);
        f->rings = rings;
        auto numbers = CCNode::create();
        numbers->setID("cats-mod-menu-frame-window-numbers");
        this->addChild(numbers, 995);
        f->numbers = numbers;
        resetResults();
    }

    void resetResults() {
        s_replaySize = cmm::cbf::replayInputs().size();
        s_results.assign(s_replaySize, {});
        auto f = m_fields.self();
        if (f->numbers) f->numbers->removeAllChildren();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        // A full restart forgets everything; a checkpoint respawn keeps the
        // windows of clicks before it.
        unsigned now = m_gameState.m_currentProgress;
        auto const& inputs = cmm::cbf::replayInputs();
        if (inputs.size() != s_results.size()) { resetResults(); return; }
        auto f = m_fields.self();
        if (f->numbers) f->numbers->removeAllChildren();
        for (size_t i = 0; i < inputs.size(); i++) {
            if (inputs[i].tick >= now) s_results[i] = {};
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto f = m_fields.self();
        auto const& inputs = cmm::cbf::replayInputs();
        if (inputs.size() != s_replaySize) resetResults();

        if (analyserOn() && !m_playerDied && m_player1 && !inputs.empty()) {
            unsigned now = m_gameState.m_currentProgress;
            unsigned ppt = std::max(1u, cmm::cbf::progressPerTick());
            // Analyse far enough ahead for the biggest CBF delay and the search range.
            int lead = std::max(searchRange(), cmm::cbf::windowTicks()) + 3;
            for (size_t i = 0; i < inputs.size(); i++) {
                auto const& in = inputs[i];
                if (in.button != 1 || in.player2 || in.tick <= now) continue;
                if ((in.tick - now) / ppt > static_cast<unsigned>(lead)) break;
                if (!s_results[i].analysed) analyse(this, inputs, i, now, ppt);
            }
            // Mark clicks the player has reached, for the rings and the tally.
            for (size_t i = 0; i < inputs.size(); i++) {
                auto const& in = inputs[i];
                if (in.tick > now) break;
                auto& r = s_results[i];
                if (r.passed || !r.analysed || !in.down || in.player2) continue;
                r.passed = true;
                r.where = m_player1->getPosition();
                if (cmm::on("framecounter") && shown(r.window) && f->numbers) {
                    auto label = CCLabelBMFont::create(std::to_string(std::max(1, r.window)).c_str(), "bigFont.fnt");
                    label->setScale(0.35f);
                    label->setColor(bandColor(r.window));
                    label->setAnchorPoint({1.f, 0.5f});
                    label->setPosition(r.where + CCPoint{-14.f, 0.f});
                    f->numbers->addChild(label);
                }
            }
        }

        // Rings and numbers live in the level: line them up with it.
        if (!f->rings) return;
        f->rings->clear();
        bool show = cmm::on("framecounter");
        if (f->numbers) {
            f->numbers->setVisible(show);
            if (show) cmm::overlay::syncTo(f->numbers, m_objectLayer, this);
        }
        if (!show) return;
        cmm::overlay::syncTo(f->rings, m_objectLayer, this);
        for (auto const& r : s_results) {
            if (!r.passed || !shown(r.window)) continue;
            auto c = bandColor(r.window);
            ccColor4F col{c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f};
            constexpr int n = 24;
            CCPoint v[n];
            for (int i = 0; i < n; i++) {
                float a = 2.f * static_cast<float>(M_PI) * i / n;
                v[i] = CCPoint(r.where.x + 9.f * std::cos(a), r.where.y + 9.f * std::sin(a));
            }
            f->rings->drawPolygon(v, n, {0, 0, 0, 0}, 0.8f, col);
        }
    }
};
