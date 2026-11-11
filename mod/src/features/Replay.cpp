// Replay bot: records your inputs tick by tick and plays them back.
//
// Ticks are GD's own m_gameState.m_currentProgress. GD rewinds it when you
// respawn at a practice checkpoint, so recording just drops every input after
// the tick you respawned at, and playback jumps to the right spot.
//
// Macros are saved as plain text in <mod save folder>/macros/<name>.txt
#include <core/Hacks.hpp>
#include <core/Log.hpp>
#include "Replay.hpp"
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace geode::prelude;

namespace cmm::replay {

    namespace {
        std::vector<Input> s_inputs;
        size_t s_next = 0;           // next input to play
        bool s_injecting = false;
        std::string s_levelName;
        int s_levelID = 0;
        char s_name[64] = "my-macro";
        std::vector<std::string> s_files;
        bool s_filesDirty = true;

        bool recording() { return cmm::on("bot.record"); }
        bool playing() { return cmm::on("bot.play"); }

        std::filesystem::path folder() {
            return std::filesystem::path((Mod::get()->getSaveDir() / "macros").native());
        }

        void refreshFiles() {
            s_files.clear();
            std::error_code ec;
            for (auto& entry : std::filesystem::directory_iterator(folder(), ec)) {
                if (entry.path().extension() == ".txt") s_files.push_back(entry.path().stem().string());
            }
            std::sort(s_files.begin(), s_files.end());
            s_filesDirty = false;
        }

        // Drop inputs at or after `tick` (respawn while recording) and find
        // where playback should continue from.
        void rewindTo(unsigned tick) {
            if (recording()) {
                s_inputs.erase(std::remove_if(s_inputs.begin(), s_inputs.end(), [&](auto const& i) { return i.tick >= tick; }), s_inputs.end());
            }
            s_next = std::lower_bound(s_inputs.begin(), s_inputs.end(), tick, [](Input const& i, unsigned t) { return i.tick < t; }) - s_inputs.begin();
        }

        bool save(std::string const& name) {
            std::error_code ec;
            std::filesystem::create_directories(folder(), ec);
            std::ofstream file(folder() / (name + ".txt"));
            if (!file) return false;
            file << "Cat's Mod Menu macro v1\n";
            file << "level=" << s_levelName << "\n";
            file << "id=" << s_levelID << "\n";
            for (auto const& i : s_inputs) {
                file << i.tick << " " << (i.down ? 1 : 0) << " " << i.button << " " << (i.player2 ? 1 : 0) << "\n";
            }
            s_filesDirty = true;
            return true;
        }

        bool parse(std::string const& name, std::vector<Input>& out, std::string* levelName, int* levelID) {
            std::ifstream file(folder() / (name + ".txt"));
            if (!file) return false;
            std::string line;
            std::getline(file, line);
            if (line.rfind("Cat's Mod Menu macro", 0) != 0) return false;
            std::vector<Input> inputs;
            while (std::getline(file, line)) {
                if (line.rfind("level=", 0) == 0) { if (levelName) *levelName = line.substr(6); continue; }
                if (line.rfind("id=", 0) == 0) { if (levelID) *levelID = std::atoi(line.c_str() + 3); continue; }
                std::istringstream ss(line);
                Input in{};
                int down = 0, p2 = 0;
                if (ss >> in.tick >> down >> in.button >> p2) {
                    in.down = down;
                    in.player2 = p2;
                    inputs.push_back(in);
                }
            }
            std::stable_sort(inputs.begin(), inputs.end(), [](auto const& a, auto const& b) { return a.tick < b.tick; });
            out = std::move(inputs);
            return true;
        }

        bool load(std::string const& name) {
            std::vector<Input> inputs;
            if (!parse(name, inputs, &s_levelName, &s_levelID)) return false;
            s_inputs = std::move(inputs);
            s_next = 0;
            return true;
        }
    }

    std::vector<std::string> macroNames() {
        refreshFiles();
        return s_files;
    }

    bool readMacro(std::string const& name, std::vector<Input>& out) {
        return parse(name, out, nullptr, nullptr);
    }

    void drawWindowContents() {
        if (auto h = find("bot.record")) {
            if (ImGui::RadioButton("Off", !recording() && !playing())) {
                if (auto r = find("bot.record")) setEnabled(*r, false);
                if (auto p = find("bot.play")) setEnabled(*p, false);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Record", recording())) {
                if (auto p = find("bot.play")) setEnabled(*p, false);
                setEnabled(*h, true);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Play", playing())) {
                setEnabled(*h, false);
                if (auto p = find("bot.play")) setEnabled(*p, true);
            }
        }

        ImGui::TextDisabled("%zu inputs%s%s", s_inputs.size(),
            s_levelName.empty() ? "" : " - ", s_levelName.c_str());

        ImGui::PushItemWidth(-1);
        ImGui::InputTextWithHint("##name", "macro name", s_name, sizeof(s_name));
        ImGui::PopItemWidth();
        float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.f;
        if (ImGui::Button("Save", {half, 0})) {
            if (s_inputs.empty()) log::warn("Nothing recorded to save");
            else if (save(s_name)) log::info("Saved macro {} ({} inputs)", s_name, s_inputs.size());
            else log::error("Could not save macro {}", s_name);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear", {half, 0})) {
            s_inputs.clear();
            s_next = 0;
        }

        if (s_filesDirty) refreshFiles();
        if (ImGui::BeginListBox("##macros", {-1, ImGui::GetTextLineHeightWithSpacing() * 5})) {
            for (auto const& f : s_files) {
                if (ImGui::Selectable(f.c_str(), f == s_name)) {
                    std::snprintf(s_name, sizeof(s_name), "%s", f.c_str());
                    if (load(f)) log::info("Loaded macro {} ({} inputs)", f, s_inputs.size());
                    else log::error("Could not load macro {}", f);
                }
            }
            ImGui::EndListBox();
        }
        if (ImGui::Button("Refresh list", {-1, 0})) s_filesDirty = true;
        ImGui::TextDisabled("Click a macro to load it.");
    }
}

using namespace cmm::replay;

class $modify(ReplayGameLayer, GJBaseGameLayer) {
    bool isMain() {
        auto pl = PlayLayer::get();
        return pl && static_cast<GJBaseGameLayer*>(pl) == this;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (isMain() && !s_injecting) {
            // While playing, your own clicks are ignored so they don't mix in.
            if (playing()) return;
            if (recording()) {
                s_inputs.push_back({m_gameState.m_currentProgress, down, button, !isPlayer1});
                s_next = s_inputs.size();
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void processQueuedButtons(float dt, bool clearInputQueue) {
        if (isMain() && playing()) {
            unsigned now = m_gameState.m_currentProgress;
            while (s_next < s_inputs.size() && s_inputs[s_next].tick <= now) {
                auto const& in = s_inputs[s_next++];
                s_injecting = true;
                this->handleButton(in.down, in.button, !in.player2);
                s_injecting = false;
            }
        }
        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
    }
};

class $modify(ReplayPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        if (recording() && level) {
            s_levelName = level->m_levelName;
            s_levelID = level->m_levelID.value();
        }
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        // After a full restart the tick is back at the start; after a checkpoint
        // respawn it's back at the checkpoint's tick.
        rewindTo(m_gameState.m_currentProgress);
    }
};
