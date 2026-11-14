#pragma once

#include <cstdint>

namespace vhm::framework {

struct Version {
    std::uint8_t major;
    std::uint8_t minor;
    std::uint8_t patch;
};

constexpr Version kLvglVersion{9, 2, 2};

enum class Platform {
    Desktop,
    Tab5,
};

enum class HalFactory {
    Desktop,
    Tab5,
};

constexpr Platform compiled_platform()
{
#if defined(PLATFORM_BUILD_DESKTOP)
    return Platform::Desktop;
#else
    return Platform::Tab5;
#endif
}

constexpr HalFactory compiled_hal_factory()
{
#if defined(PLATFORM_BUILD_DESKTOP)
    return HalFactory::Desktop;
#else
    return HalFactory::Tab5;
#endif
}

}  // namespace vhm::framework
