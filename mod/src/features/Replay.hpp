#pragma once

#include <string>
#include <vector>

// Replay Bot macros, shared with Click Before Frames.
namespace cmm::replay {
    // Where the player was the moment a click acted (Eclipse-style
    // "physics"): playback puts the player back here before the click, so
    // tiny differences can't add up over a level.
    struct Fix {
        bool ok = false;
        float x = 0.f, y = 0.f, rotation = 0.f;  // player 1
        double yVelocity = 0.0, xVelocity = 0.0;
        // Player 2 too, in dual mode and two-player levels.
        bool ok2 = false;
        float x2 = 0.f, y2 = 0.f, rotation2 = 0.f;
        double yVelocity2 = 0.0, xVelocity2 = 0.0;
    };

    struct Input {
        unsigned tick;   // GD's m_gameState.m_currentProgress when the click came in
        bool down;
        int button;
        bool player2;
        Fix fix = {};
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
    // While the bot hands GD a click: that click's position fix, else null.
    Fix const* injectingFix();
    // Index in botMacro() of the click the bot is handing to GD right now
    // (SIZE_MAX when none).
    size_t injectingIndex();

    // The Frame Window Counter's engine replays: play the macro with one
    // click (and its release; SIZE_MAX for none) moved by `deltaProgress`,
    // continuing from GD progress `resume` (the rewound checkpoint's).
    // endMove puts the macro back and continues from `resume`.
    // Fixes from `fixLimit` on: the unmoved replay (record = true) applies
    // them and notes how far each moved the player; moved replays get the
    // same corrections added to wherever the player is (the macro's own
    // drift is corrected, the moved click's effect isn't undone).
    void beginMove(size_t press, size_t release, long deltaProgress, unsigned fixLimit, unsigned resume, bool record);
    void endMove(unsigned resume);
}
