/**
 * @file scope_temperature.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-04
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <cstdint>

namespace vibescope {
// Cached frames can be repeated in FETCH. Only a new sensor observation makes
// a running temperature fresh; receipt of the same old value does not.
struct TemperatureAge {
    std::uint64_t sample_us{}, received_ms{};
    std::uint32_t config_id{};
    bool seen{};
    void observe(std::uint64_t sample, std::uint32_t config, std::uint64_t now)
    {
        if (!seen || sample != sample_us || config != config_id) {
            sample_us = sample;
            config_id = config;
            received_ms = now;
            seen = true;
        }
    }
    bool fresh(std::uint64_t now) const { return seen && now - received_ms <= 3500; }
};
}  // namespace vibescope
