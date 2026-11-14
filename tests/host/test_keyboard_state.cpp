/**
 * @file test_keyboard_state.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "test_support.hpp"
#include "hal/keyboard_state.h"
#include "apps/app_vibescope/scope_keyboard.h"

void run_keyboard_state_tests()
{
    hal::KeyboardState state;
    EXPECT_FALSE(state.connected);
    EXPECT_TRUE(state.pressedCount() == 0);
    state.connected = true;
    EXPECT_TRUE(state.apply(0, 0, true));
    EXPECT_TRUE(state.apply(4, 13, true));
    EXPECT_TRUE(state.pressedCount() == 2);
    EXPECT_TRUE(state.keys[0] && state.keys[69]);
    EXPECT_TRUE(state.apply(0, 0, false));
    EXPECT_TRUE(state.pressedCount() == 1 && state.keys[69]);
    const auto events = state.events;
    EXPECT_FALSE(state.apply(5, 0, true));
    EXPECT_FALSE(state.apply(0, 14, true));
    EXPECT_TRUE(state.events == events && state.pressedCount() == 1);
    EXPECT_TRUE(state.lastRow == 0 && state.lastColumn == 0 && !state.lastPressed);
    state.disconnect();
    EXPECT_FALSE(state.connected);
    EXPECT_TRUE(state.pressedCount() == 0 && state.events == events);
    state.connected = true;
    EXPECT_TRUE(state.apply(4, 13, false));
    EXPECT_TRUE(state.pressedCount() == 0);  // reconnect cannot leave a stuck key
    hal::KeyboardPressQueue queue;
    hal::KeyboardState chord;
    chord.connected = true;
    chord.apply(4, 0, true);
    chord.apply(3, 1, true);
    chord.apply(2, 4, true);
    queue.push({2, 4, chord.modifiers()});
    chord.apply(2, 4, false);
    chord.apply(4, 0, false);
    hal::KeyboardPress press;
    EXPECT_TRUE(queue.pop(press) && press.modifiers == (hal::KeyCtrl | hal::KeyShift));
    EXPECT_TRUE(vibescope::physical_key(press) == 'r');
    EXPECT_FALSE(queue.pop(press));
    queue.push({0, 0, 0});
    queue.clear();
    EXPECT_FALSE(queue.pop(press));
    EXPECT_TRUE(vibescope::physical_key({4, 12, 0}) == vibescope::KeyRight);
    EXPECT_TRUE(vibescope::physical_key({3, 13, 0}) == vibescope::KeyEnter);
    EXPECT_TRUE(vibescope::physical_key({5, 1, 0}) == vibescope::KeyNone);
    const auto recording = vibescope::keyboard_status_color(false, false, false, false, true, false, true, true);
    const auto running = vibescope::keyboard_status_color(false, false, false, false, false, false, true, true);
    EXPECT_TRUE(recording == 0x800000 && running == 0x006400);
}
