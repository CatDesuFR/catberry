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

    void drawWindowContents();
}
