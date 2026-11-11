#include <core/Hacks.hpp>
#include <Geode/modify/CCTextInputNode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/GameStatsManager.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

using namespace geode::prelude;

class $modify(CMMIcons, GameManager) {
    bool isIconUnlocked(int id, IconType type) {
        if (cmm::on("unlockicons")) return true;
        return GameManager::isIconUnlocked(id, type);
    }

    bool isColorUnlocked(int id, UnlockType type) {
        if (cmm::on("unlockicons")) return true;
        return GameManager::isColorUnlocked(id, type);
    }
};

class $modify(CMMStats, GameStatsManager) {
    bool isItemUnlocked(UnlockType type, int id) {
        // Item 17 is the shop's "practice music sync" unlock.
        if (cmm::on("practicemusic") && type == UnlockType::GJItem && id == 17) return true;
        return GameStatsManager::isItemUnlocked(type, id);
    }
};

class $modify(CMMCopy, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        // Password 1 means "free copy".
        if (cmm::on("copybypass") && level) level->m_password = 1;
        return LevelInfoLayer::init(level, challenge);
    }
};

class $modify(CMMTextInput, CCTextInputNode) {
    void onClickTrackNode(bool selected) {
        if (selected) {
            if (cmm::on("charlimit")) m_maxLabelLength = 99999;
            if (cmm::on("charfilter")) {
                std::string chars;
                for (char c = 32; c < 127; c++) chars += c;
                m_allowedChars = chars;
                m_filterSwearWords = false;
            }
        }
        CCTextInputNode::onClickTrackNode(selected);
    }
};

class $modify(CMMEditor, EditorUI) {
    void constrainGameLayerPosition(float x, float y) {
        if (cmm::on("freescroll")) return;
        EditorUI::constrainGameLayerPosition(x, y);
    }
};
