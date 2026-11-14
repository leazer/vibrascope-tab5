/**
 * @file device_preferences.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-11
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace hal {
inline constexpr std::array<std::uint32_t, 4> kSleepDelayMs{0, 60000, 300000, 600000};
inline constexpr std::uint8_t kSleepPowerOffImmediate = 3;
inline constexpr std::array<std::uint32_t, 4> kSleepPowerOffDelayMs{0, 60000, 300000, 0};

struct DevicePreferences {
    std::uint8_t volume{60}, brightness{100}, sleep_index{}, sleep_power_off_index{};

    std::uint32_t encode() const
    {
        return std::uint32_t{volume} | (std::uint32_t{brightness} << 8) |
               (std::uint32_t{sleep_index} << 16) | (std::uint32_t{sleep_power_off_index} << 24);
    }

    static std::optional<DevicePreferences> decode(std::uint32_t word)
    {
        DevicePreferences value{static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8),
                                static_cast<std::uint8_t>(word >> 16), static_cast<std::uint8_t>(word >> 24)};
        if (value.volume > 100 || !value.brightness || value.brightness > 100 ||
            value.sleep_index >= kSleepDelayMs.size() || value.sleep_power_off_index >= kSleepPowerOffDelayMs.size())
            return std::nullopt;
        return value;
    }
};
}  // namespace hal
