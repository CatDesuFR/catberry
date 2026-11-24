#include "Menu.hpp"
#include <core/Hacks.hpp>
#include <core/Indicator.hpp>
#include <core/Log.hpp>
#include <features/ClickBeforeFrames.hpp>
#include <features/FrameWindow.hpp>

#include <features/Replay.hpp>
namespace cmm::predict { void drawOverlay(); }
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui-cocos.hpp>
#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace cmm::jumpkeys { std::vector<std::pair<int, int>> extra(); void setExtra(std::vector<std::pair<int, int>> const& keys); }

namespace cmm::gui {
    namespace {
        bool s_open = false;
        bool s_resetNext = false;
        float s_anim = 0.f;                    // 0 = closed, 1 = fully open
        std::vector<char const*> s_animWindows; // tab windows drawn this frame, in order
        char s_search[128] = "";
        std::string s_bindTarget;   // hack waiting for a key
        ImGuiWindow* s_swipeWindow = nullptr;  // window being scrolled by a drag
        bool s_bindPopupOpen = false;

        std::string bindKey(std::string const& id) { return "bind." + id; }

        std::string keyName(int key) {
            if (key <= 0) return "";
            auto name = CCKeyboardDispatcher::get()->keyToString(static_cast<enumKeyCodes>(key));
            return name ? name : fmt::format("Key {}", key);
        }
        std::string s_iniPath;

        constexpr float BASE_WIDTH = 210.f;

        // Used for headings in popups; plain white in the grey theme.
        ImVec4 accent() {
            return {1.f, 1.f, 1.f, 1.f};
        }

        ImVec4 mix(ImVec4 a, ImVec4 b, float t) {
            return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
        }

        ImVec4 pcColor(PC pc) {
            switch (pc) {
                case PC::Allowed: return {0.31f, 0.90f, 0.39f, 1.f};
                case PC::Sometimes: return {0.31f, 0.90f, 0.39f, 1.f};  // counts as allowed
                case PC::NotAllowed: return {1.f, 0.24f, 0.24f, 1.f};
            }
            return {1, 1, 1, 1};
        }

        char const* pcName(PC pc) {
            switch (pc) {
                case PC::Allowed: return "Allowed on Pointercrate";
                case PC::Sometimes: return "Allowed on Pointercrate in most cases (read their notes)";
                case PC::NotAllowed: return "Not allowed on Pointercrate";
            }
            return "";
        }

        float uiScale() {
            return val<float>("menu.scale", 1.f);
        }

        bool matchesSearch(Hack const& hack) {
            if (s_search[0] == 0) return true;
            std::string needle = s_search, hay = hack.name;
            std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
            return hay.find(needle) != std::string::npos;
        }

        void pcDot(PC pc) {
            if (!val<bool>("menu.pcdots", true)) return;
            auto* draw = ImGui::GetWindowDrawList();
            float h = ImGui::GetFrameHeight();
            auto pos = ImGui::GetCursorScreenPos();
            float r = h * 0.16f;
            draw->AddCircleFilled({pos.x + r + 1.f, pos.y + h * 0.5f}, r, ImGui::GetColorU32(pcColor(pc)));
            ImGui::Dummy({r * 2.f + 4.f, h});
            ImGui::SameLine(0, 0);
        }

        bool isFavourite(Hack const& hack) {
            return val<bool>("fav." + hack.id, false);
        }

        void toggleFavourite(Hack& hack) {
            bool now = !isFavourite(hack);
            save<bool>("fav." + hack.id, now);
            log::info("{} {} favourites", hack.name, now ? "added to" : "removed from");
        }

        // Keybind + favourite controls, shown in every hack's options popup.
        void drawHackExtras(Hack& hack) {
            auto bound = keyName(val<int>(bindKey(hack.id), 0));
            ImGui::Text("Keybind: %s", bound.empty() ? "none" : bound.c_str());
            if (ImGui::Button(s_bindTarget == hack.id ? "Press a key..." : "Set key")) s_bindTarget = hack.id;
            ImGui::SameLine();
            if (ImGui::Button("Clear") && !bound.empty()) {
                save<int>(bindKey(hack.id), 0);
                log::info("Keybind for {}: none", hack.name);
            }
            bool fav = isFavourite(hack);
            if (ImGui::Checkbox("Favourite", &fav)) toggleFavourite(hack);
        }

        void drawOptionsButton(Hack& hack) {
            float w = ImGui::GetFrameHeight();
            ImGui::SameLine(ImGui::GetContentRegionMax().x - w);
            if (ImGui::ArrowButton("##opts", ImGuiDir_Right)) ImGui::OpenPopup("options");
            if (ImGui::BeginPopup("options")) {
                ImGui::TextColored(accent(), "%s", hack.name.c_str());
                ImGui::Separator();
                ImGui::PushItemWidth(ImGui::GetFontSize() * 10.f);
                if (hack.options) {
                    hack.options();
                    ImGui::Separator();
                }
                drawHackExtras(hack);
                ImGui::PopItemWidth();
                ImGui::EndPopup();
            }
        }

        void drawHack(Hack& hack) {
            ImGui::PushID(hack.id.c_str());
            auto bound = keyName(val<int>(bindKey(hack.id), 0));
            if (hack.action) {
                auto label = bound.empty() ? hack.name : fmt::format("{}  [{}]", hack.name, bound);
                float w = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
                if (ImGui::Button(label.c_str(), {ImGui::GetContentRegionAvail().x - w, 0})) {
                    log::info("Shortcut: {}", hack.name);
                    hack.action();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", hack.desc.c_str());
                drawOptionsButton(hack);
                ImGui::PopID();
                return;
            }
            pcDot(hack.pc);

            bool enabled = hack.enabled;
            if (ImGui::Checkbox(hack.name.c_str(), &enabled)) setEnabled(hack, enabled);
            bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
            if (!bound.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", bound.c_str());
            }
            if (hovered) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 18.f);
                ImGui::TextUnformatted(hack.desc.c_str());
                ImGui::TextColored(pcColor(hack.pc), "%s", pcName(hack.pc));
                ImGui::TextDisabled("The arrow on the right: options, keybind, favourite.");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            drawOptionsButton(hack);
            ImGui::PopID();
        }

        bool s_resetLayout = false;

        bool beginTabWindow(char const* title, int column) {
            float s = uiScale();
            float width = BASE_WIDTH * s;
            float gap = 10.f * s;
            // Lay windows out in columns, wrapping to a new row when the screen is full.
            int perRow = std::max(1, static_cast<int>((ImGui::GetIO().DisplaySize.x - gap) / (width + gap)));
            int x = column % perRow, y = column / perRow;
            ImGui::SetNextWindowPos({gap + x * (width + gap), gap + y * 420.f * s},
                s_resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
            // At most the screen's height: a longer list scrolls (it used to run
            // off the bottom with no way to reach the rest).
            float maxHeight = std::max(160.f * s, ImGui::GetIO().DisplaySize.y - 2.f * gap);
            ImGui::SetNextWindowSizeConstraints({width, 0}, {width, maxHeight});
            s_animWindows.push_back(title);
            bool open = ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
            // Drag the list up or down to scroll it, like swiping. A mostly
            // vertical drag takes over from the row it started on (so it isn't
            // clicked); sideways drags still move sliders. Windows move by
            // their title bar.
            if (open) {
                auto& io = ImGui::GetIO();
                ImGuiWindow* window = ImGui::GetCurrentWindow();
                bool startedHere = ImRect(window->InnerRect).Contains(io.MouseClickedPos[0]);
                ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.f);
                bool vertical = std::abs(drag.y) > 8.f && std::abs(drag.y) > 2.f * std::abs(drag.x);
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && startedHere && (vertical || s_swipeWindow == window)) {
                    s_swipeWindow = window;
                    if (ImGui::IsAnyItemActive()) ImGui::ClearActiveID();
                    ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
                }
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && s_swipeWindow == window) s_swipeWindow = nullptr;
            }
            return open;
        }

        void drawTab(Tab tab, int column) {
            if (beginTabWindow(tabName(tab), column)) {
                for (auto& hack : all()) {
                    if (hack.tab == tab && matchesSearch(hack)) drawHack(hack);
                }
            }
            ImGui::End();
        }

        void drawCBFWindow(int column) {
            if (beginTabWindow(tabName(Tab::CBF), column)) {
                if (auto hack = find("cbf")) drawHack(*hack);
                if (auto hack = find("prediction")) drawHack(*hack);
                if (auto hack = find("framecounter")) drawHack(*hack);
                ImGui::TextUnformatted("Stage (F8 cycles)");
                for (int i = 0; i < cbf::stageCount(); i++) {
                    static char const* kShort[] = {"Off", "2 ticks", "4 ticks", "8 ticks", "16 ticks (wave 8)", "32 ticks", "64 ticks", "96 ticks", "128 ticks"};
                    if (ImGui::RadioButton(kShort[std::clamp(i, 0, 8)], cbf::stage() == i)) cbf::setStage(i);
                }
                ImGui::Separator();
                ImGui::TextUnformatted("Replay to follow");
                ImGui::PushItemWidth(-1);
                auto const& current = cbf::replayName();
                if (ImGui::BeginCombo("##cbfreplay", current.empty() ? "(none)" : current.c_str())) {
                    if (ImGui::Selectable("(none)", current.empty())) cbf::setReplay("");
                    for (auto const& name : replay::macroNames()) {
                        if (ImGui::Selectable(name.c_str(), name == current)) cbf::setReplay(name);
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
                if (current.empty()) ImGui::TextDisabled("Record one in the Replay Bot window.");
                else ImGui::TextDisabled("%d clicks", cbf::replayClicks());
                if (ImGui::Button("Reload replay", {-1, 0})) cbf::reloadReplay();
                optBool("Write CBF-LOGS.txt", "cbf.logfile", true);
                ImGui::TextDisabled("Saved clicks: %d this session", cbf::savedThisSession());
                if (ImGui::Button("Open logs folder", {-1, 0})) {
                    utils::file::openFolder(Mod::get()->getSaveDir());
                }
                ImGui::TextDisabled("Turns the cheat indicator red.");
            }
            ImGui::End();
        }

        // Starred hacks (Shift + right-click). Only shown when there are some.
        bool drawFavouritesWindow(int column) {
            bool any = false;
            for (auto const& hack : all()) any |= isFavourite(hack);
            if (!any) return false;
            if (beginTabWindow("Favourites", column)) {
                for (auto& hack : all()) {
                    if (isFavourite(hack) && matchesSearch(hack)) drawHack(hack);
                }
            }
            ImGui::End();
            return true;
        }

        void drawBotWindow(int column) {
            if (beginTabWindow(tabName(Tab::Bot), column)) replay::drawWindowContents();
            ImGui::End();
        }

        void drawConsole() {
            auto& open = val<bool>("console.open", false);
            if (!open) return;

            float s = uiScale();
            ImGui::SetNextWindowSize({560.f * s, 300.f * s}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos({10.f * s, 420.f * s}, ImGuiCond_FirstUseEver);
            bool keep = true;
            if (ImGui::Begin("Console", &keep)) {
                static bool autoScroll = true;
                static int minLevel = 0;
                auto& showAll = val<bool>("console.all", false);
                static char const* levels[] = {"All", "Info+", "Warnings+", "Errors"};

                if (ImGui::Button("Clear")) log::clear();
                ImGui::SameLine();
                bool copy = ImGui::Button("Copy");
                ImGui::SameLine();
                ImGui::Checkbox("Auto-scroll", &autoScroll);
                ImGui::SameLine();
                if (ImGui::Checkbox("All logs", &showAll)) save<bool>("console.all", showAll);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: only Click Before Frames saves.\nOn: everything the mod logs.");
                if (showAll) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.f);
                    ImGui::Combo("##level", &minLevel, levels, 4);
                }
                ImGui::Separator();

                std::string copied;
                ImGui::BeginChild("lines", {0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
                log::withEntries([&](std::deque<log::Entry> const& entries) {
                    // Filter first, then only draw the rows that are on screen.
                    static std::vector<int> visible;
                    visible.clear();
                    for (int i = 0; i < static_cast<int>(entries.size()); i++) {
                        auto const& e = entries[i];
                        if (showAll ? static_cast<int>(e.level) >= minLevel : e.cbf) visible.push_back(i);
                    }

                    auto tagFor = [](log::Level level, ImVec4& color) -> char const* {
                        color = {0.85f, 0.85f, 0.85f, 1.f};
                        switch (level) {
                            case log::Level::Debug: color = {0.55f, 0.55f, 0.6f, 1.f}; return "DEBUG";
                            case log::Level::Info: return "INFO";
                            case log::Level::Warn: color = {1.f, 0.75f, 0.3f, 1.f}; return "WARN";
                            case log::Level::Error: color = {1.f, 0.35f, 0.35f, 1.f}; return "ERROR";
                        }
                        return "INFO";
                    };

                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(visible.size()));
                    while (clipper.Step()) {
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                            auto const& e = entries[visible[row]];
                            if (e.cbf && !showAll) {
                                ImGui::TextUnformatted(e.message.c_str());
                                continue;
                            }
                            ImVec4 color;
                            char const* tag = tagFor(e.level, color);
                            ImGui::TextDisabled("%s", e.time.c_str());
                            ImGui::SameLine();
                            ImGui::TextColored(color, "[%s] %s", tag, e.message.c_str());
                        }
                    }

                    if (copy) {
                        for (int i : visible) {
                            ImVec4 color;
                            auto const& e = entries[i];
                            if (e.cbf && !showAll) copied += e.message + "\n";
                            else copied += fmt::format("{} [{}] {}\n", e.time, tagFor(e.level, color), e.message);
                        }
                    }
                });
                if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.f) ImGui::SetScrollHereY(1.f);
                ImGui::EndChild();
                if (copy) utils::clipboard::write(copied);
            }
            ImGui::End();
            if (!keep) save<bool>("console.open", false);
        }

        void drawMenuWindow(int column) {
            if (beginTabWindow(tabName(Tab::Menu), column)) {
                ImGui::PushItemWidth(-1);
                ImGui::InputTextWithHint("##search", "Search hacks...", s_search, sizeof(s_search));
                ImGui::PopItemWidth();

                auto pc = indicator::attempt();
                ImGui::TextColored(pcColor(pc), "Indicator: %s",
                    pc == PC::NotAllowed ? "red" : "green");

                // The key that opens and closes this menu.
                ImGui::Text("Menu key: %s", keyName(val<int>("menu.key", KEY_Tab)).c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(s_bindTarget == "__menu" ? "Press a key..." : "Change")) s_bindTarget = "__menu";

                auto& console = val<bool>("console.open", false);
                if (ImGui::Checkbox("Console", &console)) save<bool>("console.open", console);

                optBool("Pointercrate dots", "menu.pcdots", true);
                optBool("Animations", "menu.anim", true);

                ImGui::TextUnformatted("UI scale");
                ImGui::PushItemWidth(-1);
                if (optFloat("##scale", "menu.scale", 1.f, 0.6f, 2.5f, "%.2fx")) applyTheme();
                ImGui::PopItemWidth();

                if (ImGui::Button("Reset window layout", {-1, 0})) {
                    s_resetNext = true;
                }
                ImGui::TextDisabled("Tab: open/close menu");
            }
            ImGui::End();
        }
    }

    // Open/close animation. ImGui has no transforms, so after the windows are
    // built we move/scale their vertices and fade their colours directly.
    static void animateWindows(float t) {
        if (t >= 1.f) return;
        float s = uiScale();
        int count = static_cast<int>(s_animWindows.size());
        for (int i = 0; i < count; i++) {
            auto* window = ImGui::FindWindowByName(s_animWindows[i]);
            if (!window || !window->DrawList) continue;

            // Each window starts a little after the previous one.
            float stagger = 0.06f * i;
            float local = std::clamp((t * (1.f + 0.06f * count) - stagger), 0.f, 1.f);
            float ease = 1.f - std::pow(1.f - local, 3.f);

            float scale = 0.88f + 0.12f * ease;
            ImVec2 offset{0.f, (1.f - ease) * -30.f * s};
            ImVec2 pivot{window->Pos.x + window->Size.x * 0.5f, window->Pos.y};

            auto move = [&](ImVec2 p) {
                return ImVec2{pivot.x + (p.x - pivot.x) * scale + offset.x, pivot.y + (p.y - pivot.y) * scale + offset.y};
            };

            auto& list = *window->DrawList;
            for (auto& v : list.VtxBuffer) {
                v.pos = move(v.pos);
                auto a = static_cast<float>((v.col >> IM_COL32_A_SHIFT) & 0xFF) * ease;
                v.col = (v.col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
            }
            for (auto& cmd : list.CmdBuffer) {
                auto a = move({cmd.ClipRect.x, cmd.ClipRect.y});
                auto b = move({cmd.ClipRect.z, cmd.ClipRect.w});
                cmd.ClipRect = {a.x, a.y, b.x, b.y};
            }
        }
    }

    void applyTheme() {
        auto& style = ImGui::GetStyle();
        style = ImGuiStyle();
        ImGui::StyleColorsDark(&style);

        style.WindowRounding = 2.f;
        style.ChildRounding = 2.f;
        style.FrameRounding = 2.f;
        style.PopupRounding = 2.f;
        style.GrabRounding = 2.f;
        style.ScrollbarRounding = 2.f;
        style.ScrollbarSize = 10.f;
        style.WindowBorderSize = 1.f;
        style.PopupBorderSize = 1.f;
        style.WindowTitleAlign = {0.f, 0.5f};
        style.WindowPadding = {6.f, 5.f};
        style.FramePadding = {4.f, 2.f};
        style.ItemSpacing = {5.f, 2.f};
        style.ItemInnerSpacing = {4.f, 3.f};
        style.GrabMinSize = 8.f;

        // Plain dark greys, no accent colour: quiet and readable.
        ImVec4 bg{0.11f, 0.11f, 0.11f, 0.97f};
        ImVec4 title{0.16f, 0.16f, 0.16f, 1.f};
        ImVec4 frame{0.19f, 0.19f, 0.19f, 1.f};
        ImVec4 hover{0.26f, 0.26f, 0.26f, 1.f};
        ImVec4 active{0.33f, 0.33f, 0.33f, 1.f};
        ImVec4 light{0.86f, 0.86f, 0.86f, 1.f};
        auto* c = style.Colors;
        c[ImGuiCol_Text] = {0.92f, 0.92f, 0.92f, 1.f};
        c[ImGuiCol_TextDisabled] = {0.52f, 0.52f, 0.52f, 1.f};
        c[ImGuiCol_WindowBg] = bg;
        c[ImGuiCol_ChildBg] = {0, 0, 0, 0};
        c[ImGuiCol_PopupBg] = {0.13f, 0.13f, 0.13f, 0.98f};
        c[ImGuiCol_Border] = {0.24f, 0.24f, 0.24f, 1.f};
        c[ImGuiCol_TitleBg] = title;
        c[ImGuiCol_TitleBgActive] = title;
        c[ImGuiCol_TitleBgCollapsed] = title;
        c[ImGuiCol_FrameBg] = frame;
        c[ImGuiCol_FrameBgHovered] = hover;
        c[ImGuiCol_FrameBgActive] = active;
        c[ImGuiCol_CheckMark] = light;
        c[ImGuiCol_SliderGrab] = {0.55f, 0.55f, 0.55f, 1.f};
        c[ImGuiCol_SliderGrabActive] = light;
        c[ImGuiCol_Button] = frame;
        c[ImGuiCol_ButtonHovered] = hover;
        c[ImGuiCol_ButtonActive] = active;
        c[ImGuiCol_Header] = frame;
        c[ImGuiCol_HeaderHovered] = hover;
        c[ImGuiCol_HeaderActive] = active;
        c[ImGuiCol_Separator] = {0.24f, 0.24f, 0.24f, 1.f};
        c[ImGuiCol_SeparatorHovered] = hover;
        c[ImGuiCol_SeparatorActive] = active;
        c[ImGuiCol_ResizeGrip] = {0, 0, 0, 0};
        c[ImGuiCol_ResizeGripHovered] = hover;
        c[ImGuiCol_ResizeGripActive] = active;
        c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
        c[ImGuiCol_ScrollbarGrab] = frame;
        c[ImGuiCol_ScrollbarGrabHovered] = hover;
        c[ImGuiCol_ScrollbarGrabActive] = active;
        c[ImGuiCol_TextSelectedBg] = {0.35f, 0.35f, 0.35f, 1.f};
        c[ImGuiCol_NavHighlight] = light;

        float s = uiScale();
        style.ScaleAllSizes(s);
        style.FontScaleMain = s;
    }

    void setup() {
        auto& io = ImGui::GetIO();
        s_iniPath = (Mod::get()->getSaveDir() / "imgui.ini").string();
        io.IniFilename = s_iniPath.c_str();
        // Dragging a window's body scrolls it; windows move by their title bar.
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        // The grey theme has narrower windows: lay them out fresh once.
        if (val<int>("menu.layoutVersion", 1) < 2) {
            s_resetNext = true;
            save<int>("menu.layoutVersion", 2);
        }

        auto font = (Mod::get()->getResourcesDir() / "Roboto-Medium.ttf").string();
        if (!io.Fonts->AddFontFromFileTTF(font.c_str(), 14.f)) {
            log::warn("Could not load menu font, using the default one");
            io.Fonts->AddFontDefault();
        }
        applyTheme();
    }

    bool isOpen() {
        return s_open;
    }

    void jumpKeyOptions() {
        auto keys = cmm::jumpkeys::extra();
        ImGui::TextUnformatted("Extra jump keys:");
        if (keys.empty()) ImGui::TextDisabled("  none yet");
        for (size_t i = 0; i < keys.size(); i++) {
            ImGui::PushID(static_cast<int>(i));
            // P1 / P2: which player the key jumps for (click to switch).
            bool p2 = keys[i].second == 2;
            ImGui::PushStyleColor(ImGuiCol_Button, p2 ? ImVec4(0.85f, 0.35f, 0.35f, 1.f) : ImVec4(0.30f, 0.55f, 0.95f, 1.f));
            if (ImGui::SmallButton(p2 ? "P2" : "P1")) {
                keys[i].second = p2 ? 1 : 2;
                cmm::jumpkeys::setExtra(keys);
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::Text("%s", keyName(keys[i].first).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                keys.erase(keys.begin() + static_cast<long>(i));
                cmm::jumpkeys::setExtra(keys);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ImGui::SmallButton(s_bindTarget == "__jump" ? "Press a key... (Esc: cancel)" : "Add a jump key")) s_bindTarget = "__jump";
        ImGui::Separator();
        ImGui::TextUnformatted("Default keys that jump:");
        optBool("Space", "jumpkeys.space", true);
        optBool("W", "jumpkeys.w", true);
        optBool("Up arrow", "jumpkeys.up", true);
    }

    void toggle() {
        s_open = !s_open;
    }

    bool captureBindKey(int key) {
        if (s_bindTarget.empty()) return false;
        if (key == KEY_Escape) {
            s_bindTarget.clear();
            return true;
        }
        // A new jump key ("__jump").
        if (s_bindTarget == "__jump") {
            auto keys = cmm::jumpkeys::extra();
            bool known = std::any_of(keys.begin(), keys.end(), [&](auto const& k) { return k.first == key; });
            if (key != KEY_Backspace && !known) keys.push_back({key, 1});
            cmm::jumpkeys::setExtra(keys);
            if (auto jk = find("jumpkeys"); jk && !jk->enabled) setEnabled(*jk, true);  // a new key should just work
            log::info("Jump key added: {}", keyName(key));
            s_bindTarget.clear();
            return true;
        }
        // The menu's own key ("__menu"): Backspace puts it back to Tab.
        if (s_bindTarget == "__menu") {
            int value = key == KEY_Backspace ? static_cast<int>(KEY_Tab) : key;
            save<int>("menu.key", value);
            log::info("Menu key: {}", keyName(value));
            s_bindTarget.clear();
            return true;
        }
        auto hack = find(s_bindTarget);
        if (hack) {
            int value = key == KEY_Backspace ? 0 : key;
            save<int>(bindKey(hack->id), value);
            log::info("Keybind for {}: {}", hack->name, value ? keyName(value) : "none");
        }
        s_bindTarget.clear();
        return true;
    }

    // ImGui key -> GD/cocos key code (the Windows virtual-key codes).
    int cocosKey(ImGuiKey k) {
        if (k >= ImGuiKey_A && k <= ImGuiKey_Z) return 0x41 + (k - ImGuiKey_A);
        if (k >= ImGuiKey_0 && k <= ImGuiKey_9) return 0x30 + (k - ImGuiKey_0);
        if (k >= ImGuiKey_F1 && k <= ImGuiKey_F12) return 0x70 + (k - ImGuiKey_F1);
        if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) return 0x60 + (k - ImGuiKey_Keypad0);
        switch (k) {
            case ImGuiKey_Space: return 0x20;
            case ImGuiKey_Enter: return 0x0D;
            case ImGuiKey_Escape: return 0x1B;
            case ImGuiKey_Backspace: return 0x08;
            case ImGuiKey_Tab: return 0x09;
            case ImGuiKey_LeftArrow: return 0x25;
            case ImGuiKey_UpArrow: return 0x26;
            case ImGuiKey_RightArrow: return 0x27;
            case ImGuiKey_DownArrow: return 0x28;
            case ImGuiKey_Insert: return 0x2D;
            case ImGuiKey_Delete: return 0x2E;
            case ImGuiKey_Home: return 0x24;
            case ImGuiKey_End: return 0x23;
            case ImGuiKey_PageUp: return 0x21;
            case ImGuiKey_PageDown: return 0x22;
            case ImGuiKey_LeftShift: case ImGuiKey_RightShift: return 0x10;
            case ImGuiKey_LeftCtrl: case ImGuiKey_RightCtrl: return 0x11;
            case ImGuiKey_LeftAlt: case ImGuiKey_RightAlt: return 0x12;
            case ImGuiKey_Comma: return 0xBC;
            case ImGuiKey_Period: return 0xBE;
            case ImGuiKey_Minus: return 0xBD;
            case ImGuiKey_Equal: return 0xBB;
            case ImGuiKey_Semicolon: return 0xBA;
            case ImGuiKey_Slash: return 0xBF;
            case ImGuiKey_Apostrophe: return 0xDE;
            case ImGuiKey_LeftBracket: return 0xDB;
            case ImGuiKey_RightBracket: return 0xDD;
            case ImGuiKey_Backslash: return 0xDC;
            case ImGuiKey_GraveAccent: return 0xC0;
            default: return 0;
        }
    }

    void drawBindPopup() {
        if (s_bindTarget.empty()) return;
        bool menuKey = s_bindTarget == "__menu";
        bool jumpKey = s_bindTarget == "__jump";  // Jump Keys: a new key
        auto hack = menuKey || jumpKey ? nullptr : find(s_bindTarget);
        if (!hack && !menuKey && !jumpKey) { s_bindTarget.clear(); return; }
        // The menu can take keyboard input before GD sees it, so read the key here too.
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
            if (!ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false)) continue;
            int code = cocosKey(static_cast<ImGuiKey>(k));
            if (code && captureBindKey(code)) return;
        }
        auto& io = ImGui::GetIO();
        ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
        if (ImGui::Begin("Keybind", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Text("Press a key for %s", menuKey ? "opening the menu" : jumpKey ? "jumping" : hack->name.c_str());
            if (ImGui::Button("Cancel")) s_bindTarget.clear();
            ImGui::TextDisabled(menuKey ? "Esc: cancel   Backspace: back to Tab" : jumpKey ? "Esc: cancel" : "Esc: cancel   Backspace: remove the keybind");
        }
        ImGui::End();
    }

    void draw() {
        auto& io = ImGui::GetIO();
        io.MouseDrawCursor = s_open;
        if (s_open) drawBindPopup();

        bool animate = val<bool>("menu.anim", true);
        float dt = std::min(io.DeltaTime, 0.05f);
        if (!animate) s_anim = s_open ? 1.f : 0.f;
        else if (s_open) s_anim = std::min(1.f, s_anim + dt / 0.25f);
        else s_anim = std::max(0.f, s_anim - dt / 0.15f);

        s_animWindows.clear();
        if (s_anim > 0.f) {
            s_resetLayout = s_resetNext;
            s_resetNext = false;
            int col = 0;
            drawMenuWindow(col++);
            if (drawFavouritesWindow(col)) col++;
            drawTab(Tab::Global, col++);
            drawTab(Tab::Player, col++);
            drawTab(Tab::Level, col++);
            drawTab(Tab::Bypass, col++);
            drawCBFWindow(col++);
            drawTab(Tab::Labels, col++);
            drawTab(Tab::Creator, col++);
            drawTab(Tab::Shortcuts, col++);
            drawBotWindow(col++);
            drawTab(Tab::Performance, col++);
            animateWindows(s_anim);
        }
        drawConsole();
        fw::drawOverlay();
    }
}
