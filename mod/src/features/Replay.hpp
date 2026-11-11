#pragma once

#include <string>
#include <vector>

// Replay Bot macros, shared with Click Before Frames.
namespace cmm::replay {
    struct Input {
        unsigned tick;   // GD's m_gameState.m_currentProgress when the click came in
        bool down;
        int button;
        bool player2;
    };

    // Names of the saved macros (macros folder in the mod's save folder).
    std::vector<std::string> macroNames();
    // Reads a macro without touching the bot's own loaded macro.
    bool readMacro(std::string const& name, std::vector<Input>& out);
    // Same, plus the level it was recorded on ("" / 0 when it wasn't saved).
    bool readMacro(std::string const& name, std::vector<Input>& out, std::string& level, int& levelID);

    // For the self-test: save the bot's current recording / load a macro into the bot.
    bool saveCurrent(std::string const& name);
    bool loadIntoBot(std::string const& name);
    size_t botInputs();
    void clearBot();
    // The bot's macro: its clicks and the level it was recorded on.
    std::vector<Input> const& botMacro();
    std::string const& botLevel();
    int botLevelID();

    void drawWindowContents();
}
