#include "../src/common/keys.h"
#include "test.h"

void testPressMerger() {
    const int F6 = parseKey("F6");
    PressMerger m;
    // One press reported by two devices: one down, one up.
    CHECK(m.feed(5, {F6, true}));
    CHECK(!m.feed(8, {F6, true}));
    CHECK(!m.feed(8, {F6, false}));
    CHECK(m.feed(5, {F6, false}));
    // Two separate presses on one device pass through.
    CHECK(m.feed(5, {F6, true}));
    CHECK(m.feed(5, {F6, false}));
    // A release from a device that never pressed it is not passed on.
    CHECK(!m.feed(9, {F6, false}));
    // Different keys are independent; wheel notches always pass.
    CHECK(m.feed(5, {F6, true}));
    CHECK(m.feed(8, {keycode::Escape, true}));
    CHECK(m.feed(5, {keycode::WheelUp, true}));
    CHECK(m.feed(8, {keycode::WheelUp, true}));
    // A device unplugged while holding keys: releases for what only it held.
    std::vector<InputEvent> released;
    m.forget(5, released);
    CHECK(released.size() == 1 && released[0].code == F6 && !released[0].down);
    released.clear();
    m.forget(8, released);
    CHECK(released.size() == 1 && released[0].code == keycode::Escape);
}
