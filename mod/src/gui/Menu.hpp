#pragma once

namespace cmm::gui {
    void setup();
    void draw();
    void applyTheme();

    bool isOpen();
    void toggle();

    // Keybinds: right-click a hack in the menu, then press a key.
    // Returns true if the key was used to set a bind (so the game shouldn't see it).
    bool captureBindKey(int key);
}
