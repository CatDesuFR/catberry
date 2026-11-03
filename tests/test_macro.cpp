#include "../src/common/macro.h"
#include "test.h"

using Names = std::vector<std::string>;

void testMacro() {
    Macro m;
    m.events = {{0, keycode::MouseLeft, true}, {1000, keycode::MouseLeft, false}};
    m.durationUs = 2000;
    CHECK(saveMacro("b", m));
    CHECK(saveMacro("a", m));
    CHECK((macroNames() == Names{"a", "b"}));

    CHECK(renameMacro("a", "c"));
    CHECK(!renameMacro("c", "b"));   // b exists
    CHECK(!renameMacro("nope", "d")); // no such macro
    CHECK(!renameMacro("c", "bad name"));
    Macro loaded;
    CHECK(loadMacro("c", loaded) && loaded.events.size() == 2);
    CHECK(deleteMacro("b"));
    CHECK(!deleteMacro("b"));
    CHECK((macroNames() == Names{"c"}));

    MacroConfig cfg;
    std::string bad;
    CHECK(parseKeyList("E,LMB", cfg.keys, bad));
    cfg.keysText = "E,LMB";
    cfg.hotkey = keycode::F6;
    cfg.count = 3;
    cfg.delayMs = 40;
    CHECK(writeConfig("x", cfg));
    MacroConfig back;
    std::string error;
    CHECK(readConfig("x", back, error));
    CHECK(back.keys == cfg.keys && back.hotkey == keycode::F6 && back.count == 3 && back.delayMs == 40);
    CHECK((configNames() == Names{"x"}));
    CHECK(deleteConfig("x"));
    CHECK(configNames().empty());
}
