// On-screen labels. Every label is its own module in the Labels tab, with its
// own corner, alignment, offset, size and opacity.
#include "ClickBeforeFrames.hpp"
#include "StartPos.hpp"
#include "Stats.hpp"
#include <core/Hacks.hpp>
#include <core/Indicator.hpp>
#include <core/Log.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <imgui.h>
#include <chrono>
#include <cmath>
#include <deque>
#include <optional>

using namespace geode::prelude;

class LabelsPlayLayer;

namespace {
    using Clock = std::chrono::steady_clock;

    std::deque<Clock::time_point> s_clickTimes;
    int s_attemptClicks = 0;
    int s_sessionDeaths = 0;
    // For the Speed label: X and time when it was last measured.
    float s_speedX = 0.f, s_speedTime = 0.f, s_speedValue = 0.f;
    Clock::time_point s_sessionStart = Clock::now();
    std::deque<float> s_frameTimes;

    ccColor3B colorFor(cmm::PC pc) {
        switch (pc) {
            case cmm::PC::Allowed: return {80, 230, 100};
            case cmm::PC::Sometimes: return {80, 230, 100};
            case cmm::PC::NotAllowed: return {255, 60, 60};
        }
        return {255, 255, 255};
    }

    std::string formatTime(double seconds) {
        int total = static_cast<int>(seconds);
        int h = total / 3600, m = (total / 60) % 60, s = total % 60;
        if (h > 0) return fmt::format("{}:{:02}:{:02}", h, m, s);
        return fmt::format("{}:{:02}", m, s);
    }

    struct Line {
        std::string text;
        ccColor3B color{255, 255, 255};
        float scale = 1.f;
        bool fullOpacity = false;
    };

    struct LabelDef {
        char const* id;
        char const* name;
        char const* desc;
        cmm::PC pc;
        int defaultCorner;
        std::function<std::optional<Line>(LabelsPlayLayer*)> make;
    };

    std::vector<LabelDef>& defs() {
        static std::vector<LabelDef> d;
        return d;
    }

    // Corners: 0 top left, 1 top center, 2 top right, 3 bottom left, 4 bottom center, 5 bottom right
    char const* kCorners[] = {"Top left", "Top center", "Top right", "Bottom left", "Bottom center", "Bottom right"};
    char const* kAligns[] = {"Left", "Center", "Right"};

    std::string key(char const* id, char const* what) {
        return fmt::format("label.{}.{}", id, what);
    }
}

namespace cmm::labels {
    // Same as GD: whole percents are rounded down. With Accurate Percentage
    // on, use its number of decimals instead.
    std::string pct(float value) {
        value = std::clamp(value, 0.f, 100.f);
        if (cmm::on("accuratepercentage")) {
            int d = std::clamp(cmm::val<int>("accuratepercentage.decimals", 2), 0, 6);
            float scale = std::pow(10.f, static_cast<float>(d));
            return fmt::format("{:.{}f}", std::floor(value * scale) / scale, d);
        }
        return fmt::format("{}", static_cast<int>(std::floor(value)));
    }
}

class $modify(LabelsInputLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        auto pl = PlayLayer::get();
        if (down && button == 1 && pl && static_cast<GJBaseGameLayer*>(pl) == this) {
            s_clickTimes.push_back(Clock::now());
            s_attemptClicks++;
        }
    }
};

class $modify(LabelsPlayLayer, PlayLayer) {
    struct Fields {
        Ref<CCNode> container;
        std::vector<Ref<CCLabelBMFont>> labels;  // one per LabelDef
        std::vector<std::string> shown;
        float fpsTimer = 0.f;
        std::string fpsText = "0 FPS";
        bool needRunStart = true;
        float runFrom = 0.f;
        float bestFrom = 0.f;
        float bestTo = 0.f;
        bool hasBest = false;
    };

    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        auto container = CCNode::create();
        container->setZOrder(1001);
        container->setID("cats-mod-menu-labels");
        this->addChild(container);
        m_fields->container = container;
        cmm::indicator::onAttemptStart();
        cmm::stats::resetAttempt();
        cmm::log::info("Entered level: {}", level ? std::string(level->m_levelName) : "?");
        return true;
    }

    void onQuit() {
        cmm::log::info("Left the level");
        PlayLayer::onQuit();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        m_fields->needRunStart = true;
        s_attemptClicks = 0;
        cmm::stats::resetAttempt();
        cmm::indicator::onAttemptStart();
    }

    void recordRunEnd() {
        auto f = m_fields.self();
        float to = std::clamp(this->getCurrentPercent(), 0.f, 100.f);
        // Keep the run that got furthest; on a tie, the one that started earlier.
        if (!f->hasBest || to > f->bestTo || (to == f->bestTo && f->runFrom < f->bestFrom)) {
            f->bestFrom = f->runFrom;
            f->bestTo = to;
            f->hasBest = true;
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool wasDead = m_playerDied;
        PlayLayer::destroyPlayer(player, object);
        if (!wasDead && m_playerDied && object != m_anticheatSpike) {
            recordRunEnd();
            s_sessionDeaths++;
        }
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        recordRunEnd();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto f = m_fields.self();

        // The run starts where the player actually is on the first frame
        // (start position / checkpoint aware), not at 0%.
        if (f->needRunStart) {
            f->runFrom = this->getCurrentPercent();
            f->needRunStart = false;
        }

        if (!m_isPaused && !m_playerDied) {
            cmm::stats::attemptFrames++;
            if (cmm::stats::noclipHitThisFrame) {
                cmm::stats::noclipDeadFrames++;
                if (!cmm::stats::noclipHitLastFrame) cmm::stats::noclipDeaths++;
            }
            cmm::stats::noclipHitLastFrame = cmm::stats::noclipHitThisFrame;
            cmm::stats::noclipHitThisFrame = false;
        }

        cmm::indicator::onFrame();

        s_frameTimes.push_back(CCDirector::get()->getDeltaTime());
        while (s_frameTimes.size() > 60) s_frameTimes.pop_front();

        auto now = Clock::now();
        while (!s_clickTimes.empty() && now - s_clickTimes.front() > std::chrono::seconds(1)) s_clickTimes.pop_front();

        this->updateLabels();
    }

    void updateLabels() {
        auto f = m_fields.self();
        if (!f->container) return;
        auto& all = defs();

        while (f->labels.size() < all.size()) {
            auto label = CCLabelBMFont::create("", "bigFont.fnt");
            label->setVisible(false);
            f->container->addChild(label);
            f->labels.push_back(label);
            f->shown.emplace_back();
        }

        auto win = CCDirector::get()->getWinSize();
        constexpr float pad = 5.f;
        float cursor[6] = {0, 0, 0, 0, 0, 0};  // how far each corner's stack has grown

        for (size_t i = 0; i < all.size(); i++) {
            auto& def = all[i];
            auto label = f->labels[i].data();

            std::optional<Line> line;
            if (cmm::on(fmt::format("label.{}", def.id))) line = def.make(this);
            if (!line) {
                label->setVisible(false);
                continue;
            }

            int corner = std::clamp(cmm::val<int>(key(def.id, "corner"), def.defaultCorner), 0, 5);
            int align = std::clamp(cmm::val<int>(key(def.id, "align"), corner % 3), 0, 2);
            float scale = cmm::val<float>(key(def.id, "scale"), 0.35f) * line->scale;
            float opacity = cmm::val<float>(key(def.id, "opacity"), 0.75f);
            float offX = cmm::val<float>(key(def.id, "x"), 0.f);
            float offY = cmm::val<float>(key(def.id, "y"), 0.f);

            bool bottom = corner >= 3;
            int col = corner % 3;  // 0 left, 1 center, 2 right
            float x = col == 0 ? pad : col == 1 ? win.width / 2.f : win.width - pad;

            label->setVisible(true);
            if (f->shown[i] != line->text) {
                label->setString(line->text.c_str());
                f->shown[i] = line->text;
            }
            label->setColor(line->color);
            label->setOpacity(static_cast<GLubyte>((line->fullOpacity ? 1.f : opacity) * 255));
            label->setScale(scale);
            label->setAnchorPoint({align * 0.5f, bottom ? 0.f : 1.f});

            float h = label->getContentSize().height * scale * 0.9f;
            float y = bottom ? pad + cursor[corner] : win.height - pad - cursor[corner];
            cursor[corner] += h;
            label->setPosition({x + offX, y + offY});
        }
    }
};

namespace {
    char const* modeName(PlayerObject* p) {
        if (p->m_isShip) return p->m_isPlatformer ? "jetpack" : "ship";
        if (p->m_isBall) return "ball";
        if (p->m_isBird) return "ufo";
        if (p->m_isDart) return "wave";
        if (p->m_isRobot) return "robot";
        if (p->m_isSpider) return "spider";
        if (p->m_isSwing) return "swing";
        return "cube";
    }

    // Replaces {tags} in the custom label text.
    std::string fillTags(LabelsPlayLayer* l, std::string const& text) {
        auto p = l->m_player1;
        auto level = l->m_level;
        auto value = [&](std::string_view tag) -> std::string {
            if (tag == "level") return level ? std::string(level->m_levelName) : "";
            if (tag == "author") return level ? std::string(level->m_creatorName) : "";
            if (tag == "id") return level ? std::to_string(level->m_levelID.value()) : "0";
            if (tag == "attempt") return std::to_string(l->m_attempts);
            if (tag == "percent") return cmm::labels::pct(l->getCurrentPercent());
            if (tag == "best") return level ? std::to_string(l->m_isPracticeMode ? level->m_practicePercent : level->m_normalPercent.value()) : "0";
            if (tag == "jumps") return std::to_string(l->m_jumps);
            if (tag == "clicks") return std::to_string(s_attemptClicks);
            if (tag == "cps") return std::to_string(s_clickTimes.size());
            if (tag == "time") return formatTime(l->m_attemptTime);
            if (tag == "x") return p ? fmt::format("{:.0f}", p->getPositionX()) : "0";
            if (tag == "y") return p ? fmt::format("{:.0f}", p->getPositionY()) : "0";
            if (tag == "mode") return p ? modeName(p) : "";
            if (tag == "noclipacc") return fmt::format("{:.2f}", cmm::stats::noclipAccuracy());
            if (tag == "noclipdeaths") return std::to_string(cmm::stats::noclipDeaths);
            return fmt::format("{{{}}}", tag);  // unknown: leave as typed
        };
        std::string out;
        size_t i = 0;
        while (i < text.size()) {
            auto open = text.find('{', i);
            if (open == std::string::npos) { out += text.substr(i); break; }
            auto close = text.find('}', open);
            if (close == std::string::npos) { out += text.substr(i); break; }
            out += text.substr(i, open - i);
            out += value(std::string_view(text).substr(open + 1, close - open - 1));
            i = close + 1;
        }
        return out;
    }
}

namespace cmm::labels {
    void registerLabels() {
        using cmm::PC;
        auto& d = defs();
        auto add = [&](LabelDef def) { d.push_back(std::move(def)); };

        add({"cheat", "Cheat Indicator", "A dot that is green / orange / red by Pointercrate's rules.", PC::Allowed, 0,
            [](LabelsPlayLayer*) -> std::optional<Line> {
                return Line{".", colorFor(cmm::indicator::attempt()), 1.6f, true};
            }});
        add({"fps", "FPS", "Frames per second.", PC::Allowed, 0, [](LabelsPlayLayer* l) -> std::optional<Line> {
            auto f = l->m_fields.self();
            f->fpsTimer -= CCDirector::get()->getDeltaTime();
            if (f->fpsTimer <= 0.f) {
                f->fpsTimer = 0.25f;
                float sum = 0.f;
                for (float t : s_frameTimes) sum += t;
                f->fpsText = fmt::format("{:.0f} FPS", sum > 0.f ? s_frameTimes.size() / sum : 0.f);
            }
            return Line{f->fpsText};
        }});
        add({"cps", "CPS", "Clicks per second and clicks this attempt.", PC::Allowed, 0, [](LabelsPlayLayer*) -> std::optional<Line> {
            return Line{fmt::format("{} CPS ({} clicks)", s_clickTimes.size(), s_attemptClicks)};
        }});
        add({"attempt", "Attempt", "Attempt number.", PC::Allowed, 0, [](LabelsPlayLayer* l) -> std::optional<Line> {
            return Line{fmt::format("Attempt {}", l->m_attempts)};
        }});
        add({"percent", "Percentage", "Your current %.", PC::Allowed, 0, [](LabelsPlayLayer* l) -> std::optional<Line> {
            return Line{pct(l->getCurrentPercent()) + "%"};
        }});
        add({"runfrom", "Run From", "Where this attempt started.", PC::Allowed, 0, [](LabelsPlayLayer* l) -> std::optional<Line> {
            return Line{"Run from " + pct(l->m_fields->runFrom) + "%"};
        }});
        add({"bestrun", "Best Run", "Your furthest run this session, e.g. 40-73%.", PC::Allowed, 0, [](LabelsPlayLayer* l) -> std::optional<Line> {
            auto f = l->m_fields.self();
            if (!f->hasBest) return std::nullopt;
            return Line{"Best run: " + pct(f->bestFrom) + "-" + pct(f->bestTo) + "%"};
        }});
        add({"leveltime", "Level Time", "Time in this attempt.", PC::Allowed, 0, [](LabelsPlayLayer* l) -> std::optional<Line> {
            return Line{formatTime(l->m_attemptTime)};
        }});
        add({"session", "Session Time", "Time since the game started.", PC::Allowed, 0, [](LabelsPlayLayer*) -> std::optional<Line> {
            return Line{"Session " + formatTime(std::chrono::duration<double>(Clock::now() - s_sessionStart).count())};
        }});
        add({"clock", "Clock", "The current time.", PC::Allowed, 2, [](LabelsPlayLayer*) -> std::optional<Line> {
            return Line{cmm::log::timestamp(false)};
        }});
        add({"testmode", "Testmode", "Shows \"Testmode\" when playing from a start position.", PC::Allowed, 0, [](LabelsPlayLayer* l) -> std::optional<Line> {
            if (!l->m_isTestMode) return std::nullopt;
            return Line{"Testmode", {255, 200, 80}};
        }});
        add({"startpos", "StartPos", "Which start position you're on (StartPos Switcher).", PC::Allowed, 0, [](LabelsPlayLayer*) -> std::optional<Line> {
            if (!cmm::on("startposswitcher") || cmm::startpos::count() == 0) return std::nullopt;
            return Line{fmt::format("StartPos {}/{}", cmm::startpos::currentIndex() + 1, cmm::startpos::count())};
        }});
        add({"noclipacc", "Noclip Accuracy", "How much of the attempt you didn't touch a hazard (noclip only).", PC::Allowed, 0, [](LabelsPlayLayer*) -> std::optional<Line> {
            if (!cmm::on("noclip")) return std::nullopt;
            bool hit = cmm::stats::noclipHitThisFrame || cmm::stats::noclipHitLastFrame;
            return Line{fmt::format("Accuracy {:.2f}%", cmm::stats::noclipAccuracy()), hit ? ccColor3B{255, 80, 80} : ccColor3B{255, 255, 255}};
        }});
        add({"noclipdeaths", "Noclip Deaths", "How many times noclip saved you this attempt.", PC::Allowed, 0, [](LabelsPlayLayer*) -> std::optional<Line> {
            if (!cmm::on("noclip")) return std::nullopt;
            return Line{fmt::format("Deaths {}", cmm::stats::noclipDeaths)};
        }});
        add({"practice", "Practice / Testmode", "Shows \"Practice\" in practice mode, or \"Testmode\" from a start position.", PC::Allowed, 0, [](LabelsPlayLayer* l) -> std::optional<Line> {
            if (l->m_isPracticeMode) return Line{"Practice"};
            if (l->m_isTestMode) return Line{"Testmode"};
            return std::nullopt;
        }});
        add({"jumps", "Jumps", "How many times you jumped this attempt.", PC::Allowed, 0, [](LabelsPlayLayer* l) -> std::optional<Line> {
            return Line{fmt::format("Jumps {}", l->m_jumps)};
        }});
        add({"position", "Position", "Player X and Y.", PC::Allowed, 0, [](LabelsPlayLayer* l) -> std::optional<Line> {
            if (!l->m_player1) return std::nullopt;
            return Line{fmt::format("X {:.0f}  Y {:.0f}", l->m_player1->getPositionX(), l->m_player1->getPositionY())};
        }});
        add({"gamemode", "Gamemode", "Your gamemode and speed.", PC::Allowed, 0, [](LabelsPlayLayer* l) -> std::optional<Line> {
            auto p = l->m_player1;
            if (!p) return std::nullopt;
            float sp = p->m_playerSpeed;
            char const* speed = sp < 0.8f ? "0.5x" : sp < 1.f ? "1x" : sp < 1.2f ? "2x" : sp < 1.4f ? "3x" : "4x";
            return Line{fmt::format("{} {}{}", modeName(p), speed, p->m_vehicleSize < 1.f ? " mini" : "")};
        }});
        add({"keys", "Keys", "The keys you're holding right now.", PC::Allowed, 0, [](LabelsPlayLayer*) -> std::optional<Line> {
            if (cmm::stats::heldKeys.empty()) return Line{"Keys: -"};
            std::string text = "Keys:";
            for (int k : cmm::stats::heldKeys) {
                auto name = CCKeyboardDispatcher::get()->keyToString(static_cast<enumKeyCodes>(k));
                text += fmt::format(" {}", name ? name : "?");
            }
            return Line{text};
        }});
        add({"bestpercent", "Best %", "Your normal and practice best on this level.", PC::Allowed, 0, [](LabelsPlayLayer* l) -> std::optional<Line> {
            if (!l->m_level || l->m_isPlatformer) return std::nullopt;
            return Line{fmt::format("Best {}% (practice {}%)", l->m_level->m_normalPercent.value(), l->m_level->m_practicePercent)};
        }});
        add({"totalattempts", "Total Attempts", "All your attempts on this level, not just this session.", PC::Allowed, 0, [](LabelsPlayLayer* l) -> std::optional<Line> {
            if (!l->m_level) return std::nullopt;
            return Line{fmt::format("Total attempts {}", l->m_level->m_attempts.value())};
        }});
        add({"objects", "Objects", "How many objects the level has.", PC::Allowed, 0, [](LabelsPlayLayer* l) -> std::optional<Line> {
            return Line{fmt::format("{} objects", l->m_objects ? l->m_objects->count() : 0)};
        }});
        add({"sessiondeaths", "Session Deaths", "Deaths since you started the game.", PC::Allowed, 0, [](LabelsPlayLayer*) -> std::optional<Line> {
            return Line{fmt::format("Deaths this session: {}", s_sessionDeaths)};
        }});
        add({"speed", "Speed", "How fast the player is moving, in units per second.", PC::Allowed, 0, [](LabelsPlayLayer* l) -> std::optional<Line> {
            if (!l->m_player1) return std::nullopt;
            // Game time from physics ticks (240 per second), so speedhack and lag don't skew it.
            float now = static_cast<float>(cmm::cbf::currentTick()) / 240.f;
            float x = l->m_player1->getPositionX();
            if (now - s_speedTime >= 0.1f || now < s_speedTime) {
                // A new attempt, a teleport or a long pause: start measuring again.
                bool fresh = now < s_speedTime || now - s_speedTime > 1.f || std::fabs(x - s_speedX) > 1000.f;
                if (fresh) s_speedValue = 0.f;
                else s_speedValue = std::fabs(x - s_speedX) / (now - s_speedTime);
                s_speedX = x;
                s_speedTime = now;
            }
            return Line{fmt::format("{:.0f} u/s", s_speedValue)};
        }});
        add({"levelinfo", "Level Info", "Level name, creator and ID.", PC::Allowed, 0, [](LabelsPlayLayer* l) -> std::optional<Line> {
            if (!l->m_level) return std::nullopt;
            std::string author = l->m_level->m_creatorName;
            return Line{fmt::format("{}{}{} ({})", std::string(l->m_level->m_levelName),
                author.empty() ? "" : " by ", author, l->m_level->m_levelID.value())};
        }});
        add({"custom", "Custom Text", "Your own text. Tags like {attempt}, {percent} and {level} are filled in (see the options).", PC::Allowed, 0,
            [](LabelsPlayLayer* l) -> std::optional<Line> {
                auto const& fmtText = cmm::val<std::string>("label.custom.text", "Attempt {attempt} - {percent}%");
                if (fmtText.empty()) return std::nullopt;
                return Line{fillTags(l, fmtText)};
            }});
        add({"cbf", "Click Before Frames", "CBF window and how many clicks it saved this attempt.", PC::Allowed, 0, [](LabelsPlayLayer*) -> std::optional<Line> {
            if (!cmm::on("cbf")) return std::nullopt;
            return Line{fmt::format("CBF {}t ({} saved)", cmm::cbf::windowTicks(), cmm::cbf::savedThisAttempt()), {200, 160, 255}};
        }});

        for (auto& def : d) {
            std::string id = def.id;
            int defCorner = def.defaultCorner;
            cmm::Hack hack{fmt::format("label.{}", def.id), def.name, def.desc, cmm::Tab::Labels, def.pc};
            hack.options = [id, defCorner] {
                auto c = key(id.c_str(), "corner");
                auto a = key(id.c_str(), "align");
                auto& corner = cmm::val<int>(c, defCorner);
                if (ImGui::Combo("Corner", &corner, kCorners, 6)) cmm::save<int>(c, corner);
                auto& align = cmm::val<int>(a, corner % 3);
                if (ImGui::Combo("Align", &align, kAligns, 3)) cmm::save<int>(a, align);
                cmm::optFloat("Offset X", key(id.c_str(), "x"), 0.f, -300.f, 300.f, "%.0f");
                cmm::optFloat("Offset Y", key(id.c_str(), "y"), 0.f, -300.f, 300.f, "%.0f");
                cmm::optFloat("Size", key(id.c_str(), "scale"), 0.35f, 0.1f, 1.5f);
                cmm::optFloat("Opacity", key(id.c_str(), "opacity"), 0.75f, 0.1f, 1.f);
                if (id == "custom") {
                    static char buf[256] = "";
                    static bool loaded = false;
                    auto& text = cmm::val<std::string>("label.custom.text", "Attempt {attempt} - {percent}%");
                    if (!loaded) { std::snprintf(buf, sizeof(buf), "%s", text.c_str()); loaded = true; }
                    if (ImGui::InputText("Text", buf, sizeof(buf))) cmm::save<std::string>("label.custom.text", buf);
                    ImGui::TextDisabled("Tags: {level} {author} {id} {attempt} {percent}\n{best} {jumps} {clicks} {cps} {time}\n{x} {y} {mode} {noclipacc} {noclipdeaths}");
                }
            };
            cmm::add(std::move(hack));
        }
    }
}
