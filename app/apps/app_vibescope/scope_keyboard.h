/**
 * @file scope_keyboard.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-11
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include "hal/keyboard_state.h"

namespace vibescope {
enum PhysicalKey : unsigned { KeyNone = 0, KeyEscape = 256, KeyTab, KeyEnter, KeyBackspace, KeyDelete,
                              KeyUp, KeyDown, KeyLeft, KeyRight };

inline unsigned physical_key(const hal::KeyboardPress& press)
{
    // A164 Normal-mode matrix from the keyboard firmware's key_value_map.
    static constexpr unsigned keys[5][14] = {
        {KeyEscape, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '+', KeyDelete},
        {'`', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '[', ']', 0},
        {KeyTab, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', ';', 0, KeyBackspace},
        {0, 0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', KeyUp, '=', KeyEnter},
        {0, 0, 'z', 'x', 'c', 'v', 'b', 'n', 'm', '.', KeyLeft, KeyDown, KeyRight, ' '}
    };
    return press.row < 5 && press.column < 14 ? keys[press.row][press.column] : KeyNone;
}

inline constexpr unsigned kKeyboardShutdownColor = 0x300000;

inline unsigned keyboard_status_color(bool shutdown, bool sleeping, bool exporting, bool calibrating,
                                      bool recording, bool armed, bool connected, bool running)
{
    if (shutdown) return kKeyboardShutdownColor;
    if (sleeping) return 0x000010;
    if (exporting) return 0x006060;
    if (calibrating) return 0x51006c;
    if (recording) return 0x800000;
    if (armed) return 0x805000;
    if (!connected) return 0x703000;
    return running ? 0x006400 : 0x001870;
}
}  // namespace vibescope
