// Sixth batch: Slider Limit Bypass, the editor (Enter plays/pauses the
// playtest, noclip and hitboxes in playtests) and Jump Keys.
#include <core/Hacks.hpp>
#include <core/Log.hpp>
#include <Geode/modify/EditorUI.hpp>
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
    std::vector<int> extra() {
        std::vector<int> keys;
        std::stringstream in(cmm::val<std::string>("jumpkeys.extra", ""));
        std::string part;
        while (std::getline(in, part, ',')) {
            try { if (int k = std::stoi(part); k > 0) keys.push_back(k); } catch (...) {}
        }
        return keys;
    }

    void setExtra(std::vector<int> const& keys) {
        std::string out;
        for (int k : keys) out += (out.empty() ? "" : ",") + std::to_string(k);
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
        auto keys = extra();
        if (std::find(keys.begin(), keys.end(), key) != keys.end()) {
            if (!repeat) layer->handleButton(down, 1, true);
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
        add({"jumpkeys", "Jump Keys", "Add keys that jump, and turn off Space, W or Up, in levels and playtests.", Player, OK,
            false, [] { cmm::gui::jumpKeyOptions(); }});
    }
}
