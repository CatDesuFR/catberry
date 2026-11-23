// Sixth batch: Slider Limit Bypass, the editor (Enter plays/pauses the
// playtest, noclip and hitboxes in playtests) and Jump Keys.
#include <core/Hacks.hpp>
#include <core/Log.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/GJScaleControl.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/SliderThumb.hpp>
#include <Geode/modify/SliderTouchLogic.hpp>
#include <algorithm>
#include <sstream>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Slider Limit Bypass: every slider (editor scale, rotation, colours, trigger
// values; settings like music volume) can be dragged past both ends, and
// reports the value it was dragged to instead of stopping at 0 or 1.
// ---------------------------------------------------------------------------
class $modify(CMMSliderTouch, SliderTouchLogic) {
    void ccTouchMoved(CCTouch* touch, CCEvent* event) {
        if (!cmm::on("sliderlimit") || !m_thumb) return SliderTouchLogic::ccTouchMoved(touch, event);
        auto pos = this->convertTouchToNodeSpace(touch);
        if (m_rotated) m_thumb->setPositionY(pos.y);
        else m_thumb->setPositionX(pos.x);
        m_thumb->activate();
        if (m_slider) m_slider->updateBar();
    }
};

class $modify(CMMSliderThumb, SliderThumb) {
    // The thumb's place on the bar as a value: 0 at one end, 1 at the other,
    // and past them when dragged further.
    float unclamped() {
        float at = m_vertical ? this->getPositionY() : this->getPositionX();
        return m_length > 0.f ? (at + m_length / 2.f) / m_length : 0.f;
    }
    bool outside() {
        float v = unclamped();
        return v < -0.0001f || v > 1.0001f;
    }

    float getValue() {
        if (cmm::on("sliderlimit") && outside()) return unclamped();
        return SliderThumb::getValue();
    }

    void setValue(float value) {
        if (!cmm::on("sliderlimit") || (value >= 0.f && value <= 1.f)) return SliderThumb::setValue(value);
        float at = value * m_length - m_length / 2.f;
        if (m_vertical) this->setPositionY(at);
        else this->setPositionX(at);
    }
};

// The editor's scale control takes the touches itself and stops at its
// bounds: past the end of the slider being dragged, scale on.
class $modify(CMMScaleControl, GJScaleControl) {
    void ccTouchMoved(CCTouch* touch, CCEvent* event) {
        GJScaleControl::ccTouchMoved(touch, event);
        if (!cmm::on("sliderlimit") || m_touchID == -1 || !m_delegate) return;
        Slider* slider = m_scaleButtonType == ScaleButtonType::X ? m_sliderX
                       : m_scaleButtonType == ScaleButtonType::Y ? m_sliderY : m_sliderXY;
        if (!slider || !slider->m_touchLogic || !slider->m_touchLogic->m_thumb) return;
        auto thumb = slider->m_touchLogic->m_thumb;
        float length = thumb->m_length;
        if (length <= 0.f) return;
        auto at = slider->m_touchLogic->convertTouchToNodeSpace(touch);
        float value = ((thumb->m_vertical ? at.y : at.x) + length / 2.f) / length;
        if (value >= 0.f && value <= 1.f) return;  // inside: GD did it
        float scale = std::max(0.01f, std::round(((m_upperBound - m_lowerBound) * value + m_lowerBound) * 100.f) / 100.f);
        thumb->setValue(value);
        if (m_scaleButtonType == ScaleButtonType::X) {
            m_delegate->scaleXChanged(scale, m_scaleLocked);
            this->updateLabelX(scale);
        } else if (m_scaleButtonType == ScaleButtonType::Y) {
            m_delegate->scaleYChanged(scale, m_scaleLocked);
            this->updateLabelY(scale);
        } else {
            m_delegate->scaleXYChanged(scale, scale, m_scaleLocked);
            this->updateLabelXY(scale);
        }
    }
};

// ---------------------------------------------------------------------------
// The editor.
// ---------------------------------------------------------------------------
namespace cmm::visual { void noclipFlash(); }

class $modify(CMMEditorUI, EditorUI) {
    // Enter: start the playtest, pause it, carry on (the playtest button).
    void keyDown(enumKeyCodes key, double timestamp) {
        if (key == KEY_Enter && cmm::on("editorenterplaytest")) {
            this->onPlaytest(nullptr);
            return;
        }
        EditorUI::keyDown(key, timestamp);
    }
};

class $modify(CMMEditorLayer, LevelEditorLayer) {
    // Noclip in playtests: the editor's own way of taking a hit.
    void playerTookDamage(PlayerObject* player) {
        if (cmm::on("noclip")) {
            bool isP2 = player == m_player2;
            if ((isP2 && cmm::val<bool>("noclip.p2", true)) || (!isP2 && cmm::val<bool>("noclip.p1", true))) return;
        }
        LevelEditorLayer::playerTookDamage(player);
    }

    // Show Hitboxes in playtests too.
    bool shouldDrawDebugHitboxes() {
        if (cmm::on("showhitboxes") && m_playbackMode != PlaybackMode::Not) return true;
        return LevelEditorLayer::shouldDrawDebugHitboxes();
    }
};

// ---------------------------------------------------------------------------
// Jump Keys: more keys that jump, and the default ones (Space, W, Up) can be
// turned off. Only while a level or a playtest is running.
// ---------------------------------------------------------------------------
namespace cmm::jumpkeys {
    // Saved as "key" (player 1) or "key:2" (player 2), comma-separated.
    std::vector<std::pair<int, int>> extra() {
        std::vector<std::pair<int, int>> keys;
        std::stringstream in(cmm::val<std::string>("jumpkeys.extra", ""));
        std::string part;
        while (std::getline(in, part, ',')) {
            try {
                auto colon = part.find(':');
                int k = std::stoi(part.substr(0, colon));
                int player = colon == std::string::npos ? 1 : std::clamp(std::stoi(part.substr(colon + 1)), 1, 2);
                if (k > 0) keys.push_back({k, player});
            } catch (...) {}
        }
        return keys;
    }

    void setExtra(std::vector<std::pair<int, int>> const& keys) {
        std::string out;
        for (auto [k, player] : keys) out += (out.empty() ? "" : ",") + std::to_string(k) + (player == 2 ? ":2" : "");
        cmm::save<std::string>("jumpkeys.extra", out);
    }

    // The level or playtest being played, if any.
    GJBaseGameLayer* playing() {
        if (auto pl = PlayLayer::get(); pl && !pl->m_isPaused && !pl->m_playerDied) return pl;
        if (auto ed = LevelEditorLayer::get(); ed && ed->m_playbackMode == PlaybackMode::Playing) return ed;
        return nullptr;
    }

    // True if the key was handled here (the game shouldn't see it).
    bool handle(int key, bool down, bool repeat) {
        if (!cmm::on("jumpkeys")) return false;
        auto layer = playing();
        if (!layer) return false;
        for (auto [k, player] : extra()) {
            if (k != key) continue;
            if (!repeat) layer->handleButton(down, 1, player == 1);
            return true;
        }
        if ((key == KEY_Space && !cmm::val<bool>("jumpkeys.space", true)) ||
            (key == KEY_W && !cmm::val<bool>("jumpkeys.w", true)) ||
            (key == KEY_Up && !cmm::val<bool>("jumpkeys.up", true))) {
            return true;
        }
        return false;
    }
}

namespace cmm::gui { void jumpKeyOptions(); }

namespace cmm {
    void registerBatch6() {
        using enum Tab;
        constexpr auto OK = PC::Allowed;

        add({"sliderlimit", "Slider Limit Bypass", "Editor and settings sliders go past both ends (bigger scale, louder music...).", Global, OK});
        add({"editorenterplaytest", "Enter Plays the Playtest", "In the editor, Enter starts the playtest, pauses it and carries on (like the playtest button).", Creator, OK});
        add({"jumpkeys", "Jump Keys", "Add keys that jump (for player 1 or 2), and turn off Space, W or Up, in levels and playtests.", Player, OK,
            false, [] { cmm::gui::jumpKeyOptions(); }});
    }
}
