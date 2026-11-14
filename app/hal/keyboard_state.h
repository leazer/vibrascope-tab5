/**
 * @file keyboard_state.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace hal {
enum KeyboardModifier : std::uint8_t { KeyCtrl = 1, KeyShift = 2, KeyAlt = 4 };

struct KeyboardPress {
    std::uint8_t row{}, column{}, modifiers{};
};

// Access is serialized by the keyboard service's state mutex.
class KeyboardPressQueue {
public:
    void push(KeyboardPress press)
    {
        if (_size == _presses.size()) return;
        _presses[(_head + _size) % _presses.size()] = press;
        ++_size;
    }

    bool pop(KeyboardPress& press)
    {
        if (!_size) return false;
        press = _presses[_head];
        _head = (_head + 1) % _presses.size();
        --_size;
        return true;
    }

    void clear() { _head = _size = 0; }

private:
    std::array<KeyboardPress, 32> _presses{};
    std::size_t _head{}, _size{};
};


struct KeyboardState {
    bool connected{};
    std::uint8_t firmwareVersion{}, brightness{}, rgbMode{};
    std::array<bool, 70> keys{};
    std::uint32_t events{};
    std::uint8_t lastRow{}, lastColumn{};
    bool lastPressed{};

    bool apply(std::uint8_t row, std::uint8_t column, bool pressed)
    {
        if (!connected || row >= 5 || column >= 14) return false;
        keys[row * 14 + column] = pressed;
        lastRow = row;
        lastColumn = column;
        lastPressed = pressed;
        ++events;
        return true;
    }
    std::uint8_t modifiers() const
    {
        // A164 Normal mode uses physical Ctrl, Aa and Alt positions.
        return (keys[4 * 14] ? KeyCtrl : 0) | (keys[3 * 14 + 1] ? KeyShift : 0) |
               (keys[4 * 14 + 1] ? KeyAlt : 0);
    }
    unsigned pressedCount() const
    {
        return static_cast<unsigned>(std::count(keys.begin(), keys.end(), true));
    }
    void disconnect()
    {
        connected = false;
        firmwareVersion = 0;
        lastPressed = false;
        keys.fill(false);
    }
};
}  // namespace hal
