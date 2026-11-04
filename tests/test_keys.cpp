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

void testMergeInOrder() {
    const int F6 = parseKey("F6");
    // Both devices' press and release were waiting; read device by device
    // they come as A-down A-up B-down B-up, but in time they overlap.
    std::vector<TimedEvent> waiting = {
        {1000, 5, {F6, true}}, {3000, 5, {F6, false}}, {1001, 8, {F6, true}}, {3001, 8, {F6, false}}};
    PressMerger m;
    std::vector<InputEvent> out = mergeInOrder(m, waiting);
    CHECK(out.size() == 2 && out[0].down && !out[1].down);
    // Separate presses stay separate, in time order.
    waiting = {{5000, 5, {F6, false}}, {4000, 5, {F6, true}}};
    out = mergeInOrder(m, waiting);
    CHECK(out.size() == 2 && out[0].down && !out[1].down);
}
