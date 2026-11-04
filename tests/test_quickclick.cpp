#include "../src/common/platform.h"
#include "../src/common/daemon.h"
#include "../src/common/output.h"
#include "../src/common/quickclick.h"
#include "test.h"

#include <deque>
#include <fstream>

namespace {
// Records what a run sends.
struct FakeOutput : Output {
    int moves = 0, presses = 0;
    const char *name() const override { return "fake"; }
    void key(int, bool) override {}
    bool canPoint() const override { return true; }
    void moveTo(int, int, int, int) override { ++moves; }
    void pointerButton(int, bool down) override { presses += down; }
};
// Input for a run: queued events, and a stop request after `stopAfter` checks.
struct FakeWatcher : KeyWatcher {
    std::deque<InputEvent> events;
    mutable int checks = 0;
    int stopAfter = -1;
    bool next(InputEvent &ev, int) override {
        if (events.empty()) return false;
        ev = events.front();
        events.pop_front();
        return true;
    }
    bool stopRequested() const override { return stopAfter >= 0 && ++checks > stopAfter; }
};
} // namespace

void testQuickClick() {
    QuickSet set;
    set.desktopW = 1920;
    set.desktopH = 1080;
    set.hotkey = parseKey("F7");
    set.gapUs = 250;
    set.points = {{192, 108, ClickType::Left}, {960, 540, ClickType::Double}, {5, 6, ClickType::Right}};
    CHECK(writeQuickSet("qa", set));
    QuickSet back;
    std::string error;
    CHECK(readQuickSet("qa", back, error));
    CHECK(back.desktopW == 1920 && back.desktopH == 1080 && back.hotkey == parseKey("F7") && back.gapUs == 250);
    CHECK(back.points.size() == 3 && back.points[1].x == 960 && back.points[1].type == ClickType::Double &&
          back.points[2].type == ClickType::Right);

    // A gap outside the range is brought into it.
    set.gapUs = 5;
    CHECK(writeQuickSet("qb", set));
    CHECK(readQuickSet("qb", back, error) && back.gapUs == kMinGapUs);

    // A broken point line is refused, naming it.
    {
        std::ofstream out(configDir() / "quickclicks" / "qbad.conf");
        out << "desktop=1920x1080\nhotkey=F7\ngap_us=100\npoint=10,20,sideways\n";
    }
    CHECK(!readQuickSet("qbad", back, error) && error.find("point=10,20,sideways") != std::string::npos);

    CHECK((quickSetNames() == std::vector<std::string>{"qa", "qb", "qbad"}));
    CHECK(deleteQuickSet("qbad") && !deleteQuickSet("qbad"));
    CHECK(!writeQuickSet("bad name", set));

    QuickPoint p = scaledPoint({960, 540, ClickType::Left}, 1920, 1080, 3840, 2160);
    CHECK(p.x == 1920 && p.y == 1080 && p.type == ClickType::Left);
    p = scaledPoint({960, 540, ClickType::Middle}, 1920, 1080, 1920, 1080);
    CHECK(p.x == 960 && p.y == 540);
    p = scaledPoint({960, 540, ClickType::Middle}, 0, 0, 1920, 1080);
    CHECK(p.x == 960 && p.y == 540);

    // A set without a usable desktop size would click wherever the pointer is.
    {
        std::ofstream out(configDir() / "quickclicks" / "qnodesk.conf");
        out << "hotkey=F7\ngap_us=100\npoint=10,20,left\n";
    }
    CHECK(!readQuickSet("qnodesk", back, error) && error.find("desktop") != std::string::npos);
    CHECK(deleteQuickSet("qnodesk"));

    // A run clicks every point; Double clicks twice.
    QuickSet run;
    run.desktopW = 1920;
    run.desktopH = 1080;
    run.hotkey = parseKey("F7");
    run.points = {{1, 1, ClickType::Left}, {2, 2, ClickType::Double}, {3, 3, ClickType::Right}};
    {
        FakeOutput out;
        FakeWatcher keys;
        HotkeyState hotkey;
        runQuickSet(run, out, keys, hotkey);
        CHECK(out.moves == 3 && out.presses == 4);
    }
    // The hotkey again (released, then pressed) stops it before the next point.
    {
        FakeOutput out;
        FakeWatcher keys;
        HotkeyState hotkey;
        hotkey.held = true; // the press that started the run
        keys.events.push_back({run.hotkey, false});
        keys.events.push_back({run.hotkey, true});
        runQuickSet(run, out, keys, hotkey);
        CHECK(out.moves == 0);
    }
    // Some keyboards report one press from two devices: the second "down",
    // with no release in between, is the same press and must not stop it.
    {
        FakeOutput out;
        FakeWatcher keys;
        HotkeyState hotkey;
        hotkey.held = true;
        keys.events.push_back({run.hotkey, true});
        runQuickSet(run, out, keys, hotkey);
        CHECK(out.moves == 3 && out.presses == 4);
    }
    // ...and the press that started a run is only a new one after its release.
    {
        HotkeyState hotkey;
        CHECK(hotkey.pressed({run.hotkey, true}, run.hotkey));   // first copy: a press
        CHECK(!hotkey.pressed({run.hotkey, true}, run.hotkey));  // second copy: not
        CHECK(!hotkey.pressed({run.hotkey, false}, run.hotkey)); // released
        CHECK(!hotkey.pressed({run.hotkey, false}, run.hotkey)); // the other device's release
        CHECK(hotkey.pressed({run.hotkey, true}, run.hotkey));   // a new press
        CHECK(!hotkey.pressed({keycode::Escape, true}, run.hotkey));
    }
    // Unload (a stop request) during a run: no more points, not all at once.
    {
        FakeOutput out;
        FakeWatcher keys;
        keys.stopAfter = 0;
        HotkeyState hotkey;
        runQuickSet(run, out, keys, hotkey);
        CHECK(out.moves == 0 && out.presses == 0);
    }

    CHECK(quickPidName("x") == "quick.x");
    ClickType t;
    CHECK(parseClickType("double", t) && t == ClickType::Double && std::string(clickTypeName(t)) == "double");
    CHECK(!parseClickType("triple", t));
}
