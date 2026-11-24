// Replay bot: records your inputs tick by tick and plays them back.
//
// Ticks are GD's own m_gameState.m_currentProgress. GD rewinds it when you
// respawn at a practice checkpoint, so recording just drops every input after
// the tick you respawned at, and playback jumps to the right spot.
//
// Timing (measured in game): a click that comes in between frames, when the
// progress is P, acts two ticks later. Playback gives each click to GD at the
// start of the tick whose progress is P + one tick, where it acts right away:
// the same tick as when it was recorded. Clicks Click Before Frames gives to
// GD at the start of a tick are recorded one tick earlier for the same reason.
//
// Accuracy (like Eclipse's bot): each click is stamped with the tick it
// really reached the player on (seen in PlayerObject::pushButton /
// releaseButton), and with where that player was at the end of that tick:
// position, rotation and speed. (A real click acts partway through its tick,
// at the moment its timestamp says; the bot can only hand clicks over at the
// start of a tick. From the end of the tick on, both runs are the same again.)
// Playback puts the player back there at the end of that tick, so tiny
// differences can't add up over a level.
//
// Macros are saved as plain text in <mod save folder>/macros/<name>.txt
// (v2 lines: tick down button player2 [x y rotation yVelocity xVelocity [same for player 2]]).
#include <core/Compat.hpp>
#include <core/Hacks.hpp>
#include <core/Players.hpp>
#include <core/Log.hpp>
#include "Replay.hpp"
#include "ClickBeforeFrames.hpp"
#include "FrameWindow.hpp"
#include <chrono>
#include <ctime>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using namespace geode::prelude;

namespace cmm::replay {

    namespace {
        std::vector<Input> s_inputs;
        size_t s_next = 0;           // next input to play
        bool s_injecting = false;
        Fix s_injectingFix;  // the fix of the click the bot is handing to GD right now
        size_t s_injectingIndex = SIZE_MAX;  // and its index in s_inputs
        std::vector<Input> s_moveSaved;      // the macro while a moved-click replay runs
        bool s_moving = false;

        unsigned s_softFrom = 0;             // fixes from here on are relative
        bool s_recordDeltas = false;
        struct FixDelta { float dx, dy, drot; double dv; float dx2, dy2, drot2; double dv2; };
        // fix (by where it puts player 1) -> correction of the unmoved replay
        std::map<std::pair<float, float>, FixDelta> s_fixDeltas;
        std::string s_levelName;
        int s_levelID = 0;
        char s_name[64] = "my-macro";
        std::string s_loadedName;      // the macro file the bot has loaded / saved last
        bool s_confirmDelete = false;
        std::vector<std::string> s_files;
        bool s_filesDirty = true;
        // Recorded clicks GD hasn't handed to the player yet (index into s_inputs).
        std::vector<size_t> s_waiting;
        constexpr unsigned kFixEvery = 30;  // ticks between regular fixes while recording
        unsigned s_applyFixesStat = 0;  // fixes applied this attempt (for the window)
        // Recorded clicks whose fix is taken at the end of their tick: the
        // start of the tick with progress `at` (index into s_inputs).
        struct PendingFix { size_t index; unsigned at; };
        std::vector<PendingFix> s_pendingFixes;
        size_t s_nextFix = 0;  // playback: next input whose fix is due

        Fix stateOf(PlayerObject* p) {
            Fix f;
            f.ok = true;
            f.x = p->m_position.x;
            f.y = p->m_position.y;
            f.rotation = p->getRotation();
            f.yVelocity = p->m_yVelocity;
            f.xVelocity = p->m_platformerXVelocity;
            return f;
        }

        bool recording() { return cmm::on("bot.record"); }
        bool playing() { return cmm::on("bot.play"); }

        // The loaded macro was recorded in this level (by its ID, or its name
        // for levels without one).
        bool fitsLevel(PlayLayer* pl) {
            if (s_inputs.empty() || !pl || !pl->m_level) return false;
            int id = pl->m_level->m_levelID.value();
            if (s_levelID != 0 && id != 0) return s_levelID == id;
            auto lower = [](std::string t) {
                std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
                while (!t.empty() && t.back() == ' ') t.pop_back();
                return t;
            };
            if (s_levelName.empty()) return true;
            auto a = lower(s_levelName), b = lower(std::string(pl->m_level->m_levelName));
            if (a == b) return true;
            // A renamed copy of your own level ("clairvoyaance" -> "clairvoyance"):
            // up to 2 letters different, for names of 5 letters or more.
            if (a.size() < 5 || b.size() < 5) return false;
            std::vector<size_t> row(b.size() + 1);
            for (size_t j = 0; j <= b.size(); j++) row[j] = j;
            for (size_t i = 1; i <= a.size(); i++) {
                size_t diag = row[0];
                row[0] = i;
                for (size_t j = 1; j <= b.size(); j++) {
                    size_t up = row[j];
                    row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0 : 1)});
                    diag = up;
                }
            }
            return row[b.size()] <= 2;
        }

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
        // When the bot hands a click to GD (the progress before that tick):
        // at the tick it was stamped with, where your click acted. A click
        // with no position fix is one the bot added at a practice respawn,
        // meant for the first tick after it: one tick later.
        // Option "bot.sooner" (on): clicks with a fix at their stamped tick,
        // which matches recordings of real play (your presses act partway into
        // a tick). Off: a tick later, as before 1.1.6 (matches clicks made
        // exactly between frames).
        unsigned dueAt(Input const& in) {
            bool later = in.button != 0 && (!in.fix.ok || !cmm::val<bool>("bot.sooner", true));
            return later ? in.tick + cmm::cbf::progressPerTick() : in.tick;
        }

        void rewindTo(unsigned tick) {
            if (recording()) {
                s_inputs.erase(std::remove_if(s_inputs.begin(), s_inputs.end(), [&](auto const& i) { return i.tick >= tick; }), s_inputs.end());
            }
            unsigned ppt0 = cmm::cbf::progressPerTick();
            s_next = std::lower_bound(s_inputs.begin(), s_inputs.end(), tick >= ppt0 ? tick - ppt0 : 0, [](Input const& i, unsigned t) { return i.tick < t; }) - s_inputs.begin();
            while (s_next < s_inputs.size() && dueAt(s_inputs[s_next]) < tick) s_next++;

            s_waiting.clear();
            s_pendingFixes.clear();
            s_applyFixesStat = 0;
            unsigned ppt = cmm::cbf::progressPerTick();
            s_nextFix = std::lower_bound(s_inputs.begin(), s_inputs.end(), tick, [ppt](Input const& i, unsigned t) { return i.tick + 2 * ppt < t; }) - s_inputs.begin();
        }

        bool save(std::string const& name) {
            std::error_code ec;
            std::filesystem::create_directories(folder(), ec);
            std::ofstream file(folder() / (name + ".txt"));
            if (!file) return false;
            // Stamping can move a click a tick later: keep the list in order.
            s_waiting.clear();
            s_pendingFixes.clear();
            std::stable_sort(s_inputs.begin(), s_inputs.end(), [](auto const& a, auto const& b) { return a.tick < b.tick; });
            file << "Cat's Mod Menu macro v2\n";
            file << "level=" << s_levelName << "\n";
            file << "id=" << s_levelID << "\n";
            for (auto const& i : s_inputs) {
                file << i.tick << " " << (i.down ? 1 : 0) << " " << i.button << " " << (i.player2 ? 1 : 0);
                if (i.fix.ok) {
                    file << fmt::format(" {:.9g} {:.9g} {:.9g} {:.17g} {:.17g}", i.fix.x, i.fix.y, i.fix.rotation, i.fix.yVelocity, i.fix.xVelocity);
                    if (i.fix.ok2) {
                        file << fmt::format(" {:.9g} {:.9g} {:.9g} {:.17g} {:.17g}", i.fix.x2, i.fix.y2, i.fix.rotation2, i.fix.yVelocity2, i.fix.xVelocity2);
                    }
                }
                file << "\n";
            }
            s_filesDirty = true;
            s_loadedName = name;
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
                    Fix fix;
                    if (ss >> fix.x >> fix.y >> fix.rotation >> fix.yVelocity >> fix.xVelocity) {
                        fix.ok = true;
                        if (ss >> fix.x2 >> fix.y2 >> fix.rotation2 >> fix.yVelocity2 >> fix.xVelocity2) fix.ok2 = true;
                        in.fix = fix;
                    }
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
            s_nextFix = 0;
            s_loadedName = name;
            return true;
        }

        // File names can't hold / \ : * ? " < > |
        std::string safeName(std::string name) {
            for (auto& c : name) {
                if (std::string_view("/\\:*?\"<>|").find(c) != std::string_view::npos) c = '_';
            }
            while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
            return name.empty() ? "macro" : name;
        }
    }

    Fix const* injectingFix() { return s_injecting && s_injectingFix.ok ? &s_injectingFix : nullptr; }
    size_t injectingIndex() { return s_injecting ? s_injectingIndex : SIZE_MAX; }
    bool macroFits(PlayLayer* pl) { return fitsLevel(pl); }

    void beginMove(size_t press, size_t release, long deltaProgress, unsigned fixLimit, unsigned resume, bool record) {
        if (recording()) return;
        if (!s_moving) s_moveSaved = s_inputs;
        else s_inputs = s_moveSaved;
        s_moving = true;
        unsigned ppt = cmm::cbf::progressPerTick();
        auto shift = [&](size_t i) {
            if (i >= s_inputs.size()) return;
            long t = static_cast<long>(s_inputs[i].tick) + deltaProgress;
            s_inputs[i].tick = static_cast<unsigned>(std::max(0L, t));
        };
        shift(press);
        shift(release);
        s_softFrom = fixLimit;
        s_recordDeltas = record;
        if (record) s_fixDeltas.clear();
        std::stable_sort(s_inputs.begin(), s_inputs.end(), [](auto const& a, auto const& b) { return a.tick < b.tick; });
        (void)ppt;
        rewindTo(resume);
    }

    void endMove(unsigned resume) {
        if (s_moving) {
            s_inputs = std::move(s_moveSaved);
            s_moveSaved.clear();
            s_moving = false;
        }
        rewindTo(resume);
    }

    std::vector<std::string> macroNames() {
        refreshFiles();
        return s_files;
    }

    bool readMacro(std::string const& name, std::vector<Input>& out) {
        return parse(name, out, nullptr, nullptr);
    }

    bool readMacro(std::string const& name, std::vector<Input>& out, std::string& level, int& levelID) {
        level.clear();
        levelID = 0;
        return parse(name, out, &level, &levelID);
    }

    bool saveCurrent(std::string const& name) { return save(name); }
    bool loadIntoBot(std::string const& name) { return load(name); }
    size_t botInputs() { return s_inputs.size(); }
    std::vector<Input> const& botMacro() { return s_inputs; }
    std::string const& botLevel() { return s_levelName; }
    int botLevelID() { return s_levelID; }

    void clearBot() {
        s_inputs.clear();
        s_next = 0;
        s_nextFix = 0;
        s_pendingFixes.clear();
        s_loadedName.clear();
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

        // What the bot has right now, clearly.
        ImGui::Separator();
        if (s_loadedName.empty()) ImGui::TextDisabled(s_inputs.empty() ? "No macro loaded" : "Unsaved recording");
        else ImGui::Text("Loaded: %s", s_loadedName.c_str());
        size_t clicks = std::count_if(s_inputs.begin(), s_inputs.end(), [](Input const& i) { return i.button != 0; });
        ImGui::TextDisabled("%zu clicks%s%s", clicks, s_levelName.empty() ? "" : " - ", s_levelName.c_str());
        if (!s_inputs.empty()) {
            size_t fixes = std::count_if(s_inputs.begin(), s_inputs.end(), [](Input const& i) { return i.fix.ok; });
            if (fixes == 0) ImGui::TextDisabled("Old macro: no position fixes (record it again for full accuracy)");
            else if (playing()) ImGui::TextDisabled("Position fixes: %zu (%u used this attempt)", fixes, s_applyFixesStat);
            optBool("Hand clicks over a tick sooner (real recordings)", "bot.sooner", true);
        }
        if (recording()) ImGui::TextDisabled("Finishing the level in practice saves it automatically.");
        ImGui::Separator();

        ImGui::PushItemWidth(-1);
        ImGui::InputTextWithHint("##name", "macro name", s_name, sizeof(s_name));
        ImGui::PopItemWidth();
        float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.f;
        if (ImGui::Button("Save", {half, 0})) {
            auto name = safeName(s_name);
            if (s_inputs.empty()) log::warn("Nothing recorded to save");
            else if (save(name)) log::info("Saved macro {} ({} clicks)", name, s_inputs.size());
            else log::error("Could not save macro {}", name);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear", {half, 0})) {
            s_inputs.clear();
            s_next = 0;
            s_loadedName.clear();
        }

        if (s_filesDirty) refreshFiles();
        if (ImGui::BeginListBox("##macros", {-1, ImGui::GetTextLineHeightWithSpacing() * 6})) {
            for (auto const& f : s_files) {
                bool selected = f == s_loadedName;
                auto label = selected ? "> " + f : f;
                if (ImGui::Selectable(label.c_str(), selected)) {
                    std::snprintf(s_name, sizeof(s_name), "%s", f.c_str());
                    s_confirmDelete = false;
                    if (load(f)) log::info("Loaded macro {} ({} clicks)", f, s_inputs.size());
                    else log::error("Could not load macro {}", f);
                }
            }
            ImGui::EndListBox();
        }
        ImGui::TextDisabled("Click a macro to load it.");

        float third = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.f;
        if (ImGui::Button("Refresh list", {third, 0})) s_filesDirty = true;
        ImGui::SameLine();
        bool canDelete = !s_loadedName.empty();
        if (!canDelete) ImGui::BeginDisabled();
        if (ImGui::Button(s_confirmDelete ? "Really delete?" : "Delete", {third, 0})) {
            if (!s_confirmDelete) {
                s_confirmDelete = true;
            } else {
                std::error_code ec;
                std::filesystem::remove(folder() / (s_loadedName + ".txt"), ec);
                if (ec) log::error("Could not delete macro {}: {}", s_loadedName, ec.message());
                else log::info("Deleted macro {}", s_loadedName);
                s_loadedName.clear();
                s_confirmDelete = false;
                s_filesDirty = true;
            }
        }
        if (!canDelete) ImGui::EndDisabled();
        if (s_confirmDelete && ImGui::IsItemHovered()) ImGui::SetTooltip("Deletes %s for good. Click again to confirm.", s_loadedName.c_str());
    }
}

using namespace cmm::replay;

class $modify(ReplayGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        // First: keep the players' start-of-tick state before any other
        // feature (Click Before Frames, the counter...) hands GD a click.
        (void)self.setHookPriority("GJBaseGameLayer::processCommands", Priority::First);
    }

    bool isMain() {
        auto pl = PlayLayer::get();
        return pl && static_cast<GJBaseGameLayer*>(pl) == this;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (isMain() && !s_injecting) {
            // While playing a macro of this level, your own clicks are ignored
            // so they don't mix in. (In other levels you play yourself.)
            if (playing() && fitsLevel(PlayLayer::get())) return;
            if (recording()) {
                unsigned tick = m_gameState.m_currentProgress;
                // A click Click Before Frames gives GD at the start of a tick acts
                // one tick sooner than one made between frames: store it to match.
                unsigned ppt = cmm::cbf::progressPerTick();
                if (cmm::cbf::injecting() && tick >= ppt) tick -= ppt;
                // A recording belongs to the level it's made in.
                if (auto pl = PlayLayer::get(); pl && pl->m_level) {
                    s_levelName = pl->m_level->m_levelName;
                    s_levelID = pl->m_level->m_levelID.value();
                }
                s_inputs.push_back({tick, down, button, !isPlayer1});
                s_waiting.push_back(s_inputs.size() - 1);
                s_next = s_inputs.size();
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    // At the start of each tick, give GD the clicks that act on it.
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        unsigned now = m_gameState.m_currentProgress;  // progress before this tick
        unsigned ppt = cmm::cbf::progressPerTick();
        // Recording: every kFixEvery ticks keep where the players are, with no
        // click (button 0). A long stretch without clicks then can't drift
        // away either (and a practice respawn in it is covered too).
        if (isMain() && recording() && now >= 2 * ppt && now % (kFixEvery * ppt) == 0 && m_player1 && !m_player1->m_isDead) {
            Input in{now - 2 * ppt, false, 0, false};
            in.fix = stateOf(m_player1);
            if (m_player2 && (m_gameState.m_isDualMode || cmm::twoPlayer())) {
                Fix two = stateOf(m_player2);
                in.fix.ok2 = true;
                in.fix.x2 = two.x;
                in.fix.y2 = two.y;
                in.fix.rotation2 = two.rotation;
                in.fix.yVelocity2 = two.yVelocity;
                in.fix.xVelocity2 = two.xVelocity;
            }
            s_inputs.push_back(in);
            s_next = s_inputs.size();
        }
        // Recording: the end of a click's tick is now; keep where the player is.
        if (isMain() && recording() && !s_pendingFixes.empty()) {
            for (auto it = s_pendingFixes.begin(); it != s_pendingFixes.end();) {
                if (it->at != now) { ++it; continue; }
                if (it->index < s_inputs.size() && m_player1 && !m_player1->m_isDead) {
                    // Both players' state when there are two to keep in line.
                    auto& in = s_inputs[it->index];
                    in.fix = stateOf(m_player1);
                    if (m_player2 && (m_gameState.m_isDualMode || cmm::twoPlayer())) {
                        Fix two = stateOf(m_player2);
                        in.fix.ok2 = true;
                        in.fix.x2 = two.x;
                        in.fix.y2 = two.y;
                        in.fix.rotation2 = two.rotation;
                        in.fix.yVelocity2 = two.yVelocity;
                        in.fix.xVelocity2 = two.xVelocity;
                    }
                }
                it = s_pendingFixes.erase(it);
            }
        }
        // The bot plays its macro only in the level it was recorded in (in
        // another level its clicks and fixes would mix into your own play).
        if (isMain() && playing() && fitsLevel(PlayLayer::get())) {
            // The end of a click's tick: put the player where the recording had
            // it. Only right on time: after a lag spike a late fix must not
            // teleport the player backwards.
            while (s_nextFix < s_inputs.size() && s_inputs[s_nextFix].tick + 2 * ppt <= now) {
                auto const& in = s_inputs[s_nextFix++];
                if (!in.fix.ok || in.tick + 2 * ppt != now) continue;
                // A moved-click replay: past the move, corrections are relative.
                if (s_moving && in.tick + 2 * ppt > s_softFrom) {
                    auto p1 = m_player1;
                    auto p2 = in.fix.ok2 ? m_player2 : nullptr;
                    if (s_recordDeltas) {
                        FixDelta d{};
                        if (p1) { d.dx = in.fix.x - p1->m_position.x; d.dy = in.fix.y - p1->m_position.y; d.drot = in.fix.rotation - p1->getRotation(); d.dv = in.fix.yVelocity - p1->m_yVelocity; }
                        if (p2) { d.dx2 = in.fix.x2 - p2->m_position.x; d.dy2 = in.fix.y2 - p2->m_position.y; d.drot2 = in.fix.rotation2 - p2->getRotation(); d.dv2 = in.fix.yVelocity2 - p2->m_yVelocity; }
                        s_fixDeltas[{in.fix.x, in.fix.y}] = d;
                    } else {
                        auto it = s_fixDeltas.find({in.fix.x, in.fix.y});
                        if (it == s_fixDeltas.end()) continue;
                        auto add = [](PlayerObject* p, float dx, float dy, float drot, double dv) {
                            if (!p || p->m_isDead) return;
                            p->m_position = CCPoint(p->m_position.x + dx, p->m_position.y + dy);
                            p->m_lastPosition = p->m_position;
                            p->setPosition(p->m_position);
                            p->setRotation(p->getRotation() + drot);
                            p->m_yVelocity += dv;
                        };
                        add(p1, it->second.dx, it->second.dy, it->second.drot, it->second.dv);
                        add(p2, it->second.dx2, it->second.dy2, it->second.drot2, it->second.dv2);
                        continue;
                    }
                }
                auto put = [now](PlayerObject* p, float x, float y, float rotation, double yVel, double xVel) {
                    if (!p || p->m_isDead) return;
                    float moved = std::hypot(p->m_position.x - x, p->m_position.y - y);
                    if (moved > 0.5f) log::debug("Replay fix at progress {} moved the player {:.2f} units (yVel {:.3f} -> {:.3f})", now, moved, p->m_yVelocity, yVel);
                    p->m_position = CCPoint(x, y);
                    p->m_lastPosition = CCPoint(x, y);  // no sweep from where the bot was
                    p->setPosition(CCPoint(x, y));
                    p->setRotation(rotation);
                    p->m_yVelocity = yVel;
                    if (p->m_isPlatformer) p->m_platformerXVelocity = xVel;
                };
                put(m_player1, in.fix.x, in.fix.y, in.fix.rotation, in.fix.yVelocity, in.fix.xVelocity);
                if (in.fix.ok2) put(m_player2, in.fix.x2, in.fix.y2, in.fix.rotation2, in.fix.yVelocity2, in.fix.xVelocity2);
                s_applyFixesStat++;
                if (in.button == 0) cmm::fw::noteBotFix(in.fix, now);
            }
            // A click goes to GD at the start of the tick it was stamped with:
            // that's when your own click acted (one tick later, as before
            // 1.1.6, every bot click came a tick late: the fixes kept moving
            // the wave 4.8-9.6 units and it died on Slaughterhouse).
            while (s_next < s_inputs.size() && dueAt(s_inputs[s_next]) <= now) {
                auto const& in = s_inputs[s_next++];
                if (in.button == 0) continue;  // a respawn's fix only, no click
                s_injecting = true;
                s_injectingFix = in.fix;
                s_injectingIndex = s_next - 1;
                this->handleButton(in.down, in.button, !in.player2);
                s_injecting = false;
                s_injectingFix = {};
                s_injectingIndex = SIZE_MAX;
            }
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
};

class $modify(ReplayPlayLayer, PlayLayer) {
    bool init(CMM_PL_INIT_PARAMS) {
        if (!PlayLayer::init(CMM_PL_INIT_ARGS)) return false;
        if (recording() && level) {
            s_levelName = level->m_levelName;
            s_levelID = level->m_levelID.value();
        }
        return true;
    }

    // Finishing a level in practice while recording saves the macro.
    void levelComplete() {
        PlayLayer::levelComplete();
        if (!recording() || !m_isPracticeMode || s_inputs.empty()) return;
        std::string name = s_name;
        if (name.empty() || name == "my-macro") {
            auto t = std::time(nullptr);
            char stamp[32];
            std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M", std::localtime(&t));
            name = fmt::format("{} {}", m_level ? std::string(m_level->m_levelName) : "level", stamp);
        }
        name = safeName(name);
        if (save(name)) {
            std::snprintf(s_name, sizeof(s_name), "%s", name.c_str());
            log::info("Practice run finished: macro saved as {} ({} clicks)", name, s_inputs.size());
        } else {
            log::error("Practice run finished but the macro could not be saved as {}", name);
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        // After a full restart the tick is back at the start; after a checkpoint
        // respawn it's back at the checkpoint's tick.
        unsigned now = m_gameState.m_currentProgress;
        rewindTo(now);
        // Recording in practice: GD puts the player back at the checkpoint
        // from what it saved there, which isn't exactly where a run straight
        // through is at that moment. Keep that state as a fix with no click
        // (button 0), so playback puts the player exactly there too.
        unsigned ppt = cmm::cbf::progressPerTick();
        if (recording() && now >= 2 * ppt && m_player1 && !m_player1->m_isDead && cmm::val<bool>("bot.respawnfix", true)) {
            Input in{now - 2 * ppt, false, 0, false};
            in.fix = stateOf(m_player1);
            if (m_player2 && (m_gameState.m_isDualMode || cmm::twoPlayer())) {
                Fix two = stateOf(m_player2);
                in.fix.ok2 = true;
                in.fix.x2 = two.x;
                in.fix.y2 = two.y;
                in.fix.rotation2 = two.rotation;
                in.fix.yVelocity2 = two.yVelocity;
                in.fix.xVelocity2 = two.xVelocity;
            }
            s_inputs.push_back(in);
            // A button held when you died: the respawn drops the clicks after
            // the checkpoint, its release too, so the macro would keep holding
            // it. Match what GD holds now (it acts on the first tick after
            // the respawn, like a click handed over then).
            for (int button = 1; button <= 3; button++) {
                bool macroHeld = false, flag = false;
                for (auto const& i : s_inputs) {
                    if (i.button != button || (cmm::twoPlayer() && i.player2)) continue;
                    macroHeld = i.down;
                    flag = i.player2;
                }
                auto const& holding = m_player1->m_holdingButtons;
                auto it = holding.find(button);
                bool gdHeld = it != holding.end() && it->second;
                if (macroHeld != gdHeld) {
                    s_inputs.push_back({now - ppt, gdHeld, button, flag});
                    log::debug("Respawn at progress {}: button {} is {} now, added a {}", now, button, gdHeld ? "held" : "not held",
                        gdHeld ? "press" : "release");
                }
            }
            s_next = s_inputs.size();
            log::debug("Respawn at progress {}: kept the player's state as a fix", now);
        }
    }
};

// Stamps recorded clicks with the tick they really reached the player on,
// and where the player was right then.
class $modify(ReplayPlayer, PlayerObject) {
    void stamp(PlayerButton button, bool down) {
        if (!recording() || s_waiting.empty()) return;
        auto pl = PlayLayer::get();
        if (!pl || (this != pl->m_player1 && this != pl->m_player2)) return;  // hidden players
        bool p2 = this == pl->m_player2;
        for (auto it = s_waiting.begin(); it != s_waiting.end(); ++it) {
            if (*it >= s_inputs.size()) continue;
            auto& in = s_inputs[*it];
            if (in.down != down || in.button != static_cast<int>(button)) continue;
            // The first player that gets a click decides its tick. In a one-player level both jump keys move player 1 (and both
            // players in dual mode): only a two-player level keeps them apart.
            if (cmm::twoPlayer() && in.player2 != p2) continue;
            unsigned ppt = cmm::cbf::progressPerTick();
            unsigned act = pl->m_gameState.m_currentProgress;
            in.tick = act >= ppt ? act - ppt : 0;
            // Its fix: where the player is at the end of this tick (taken at the
            // start of the next one). Before the first tick there's nothing to
            // line up with.
            in.fix = {};
            if (act >= ppt) s_pendingFixes.push_back({static_cast<size_t>(*it), act + ppt});
            s_waiting.erase(it);
            break;
        }
    }

    bool pushButton(PlayerButton button) {
        stamp(button, true);
        return PlayerObject::pushButton(button);
    }

    bool releaseButton(PlayerButton button) {
        stamp(button, false);
        return PlayerObject::releaseButton(button);
    }
};
