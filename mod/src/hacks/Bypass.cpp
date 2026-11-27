#include <core/Compat.hpp>
#include <core/Hacks.hpp>
#include <Geode/modify/CCTextInputNode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/GameStatsManager.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <cstring>
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

// Practice Music Sync: the level song keeps playing in practice mode, with
// GD's own option ("0125", normally a shop unlock) switched on for you.
class $modify(CMMPracticeMusicGM, GameManager) {
    bool getGameVariable(char const* key) {
        if (cmm::on("practicemusic") && key && std::strcmp(key, "0125") == 0) return true;
        return GameManager::getGameVariable(key);
    }
};

class $modify(CMMPracticeMusicPL, PlayLayer) {
    // GD swaps in the practice song when practice mode turns on: keep the
    // level song instead, and let GD keep it in sync with checkpoints.
#if CMM_GD_2091
    void playPracticeMusic() {
        if (!cmm::on("practicemusic")) return PlayLayer::playPracticeMusic();
        m_practiceMusicSync = true;
    }
#else
    // 2.2081 has no separate call for it: start the level song again after
    // practice mode put the practice song on.
    void togglePracticeMode(bool practice) {
        PlayLayer::togglePracticeMode(practice);
        if (practice && cmm::on("practicemusic")) {
            m_practiceMusicSync = true;
            this->startMusic();
        }
    }
#endif
    void resetLevel() {
        if (cmm::on("practicemusic") && m_isPracticeMode) m_practiceMusicSync = true;
        PlayLayer::resetLevel();
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
