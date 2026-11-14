/**
 * @file scope_ppa.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <cstdint>

namespace vibescope {
struct PpaStats {
    bool initialized{};
    std::uint32_t alignment{};
    std::uint64_t image_tasks{}, fill_tasks{}, pixels{}, elapsed_us{}, max_us{};
    std::uint64_t fallbacks{}, rejected{}, errors{};
    // Cumulative diagnostics; software timings require LV_USE_OS=0 / one SW unit.
    std::uint64_t cache_us{}, image_us{}, fill_us{};
    std::uint64_t dma_image_tasks{};
    std::uint64_t sw_us[14]{}, sw_tasks[14]{};
};

// Call after lv_init(), with the application's LVGL lock held. Idempotent.
// False leaves the existing software renderer fully operational.
bool init_ppa_draw();
// Read under the same LVGL lock; counters are cumulative across app entries.
const PpaStats& ppa_stats();
}  // namespace vibescope
