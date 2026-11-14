/**
 * @file test_scope_render_cache.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-04
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "test_support.hpp"
#include "apps/app_vibescope/scope_render_state.h"
#include "apps/app_vibescope/scope_raster.h"
#include "hal/status_state.h"
#include "hal/device_preferences.h"
#include <array>
#include <vector>

void run_scope_render_cache_tests()
{
    {
        hal::PowerStatusFilter supply;
        supply.update(true, 1.82F, 0.F, false);
        EXPECT_FALSE(supply.state.valid);
        supply.update(true, 1.82F, 0.F, false);
        supply.update(true, 1.82F, 0.F, false);
        EXPECT_TRUE(supply.state.valid && supply.state.battery == hal::BatteryPresence::Absent);
        for (int i = 0; i < 3; ++i) supply.update(true, 7.6F, .2F, false);
        EXPECT_TRUE(supply.state.battery == hal::BatteryPresence::Present && !supply.state.charging);
        for (int i = 0; i < 3; ++i) supply.update(true, 7.6F, -.3F, true);
        EXPECT_TRUE(supply.state.charging);
        // Retain a known pack at the end of charge. An unconfirmed pack stays unknown.
        for (int i = 0; i < 3; ++i) supply.update(true, 8.4F, 0.F, true);
        EXPECT_TRUE(supply.state.battery == hal::BatteryPresence::Present && !supply.state.charging);
        hal::PowerStatusFilter empty;
        for (int i = 0; i < 3; ++i) empty.update(true, 8.4F, 0.F, true);
        EXPECT_TRUE(empty.state.battery == hal::BatteryPresence::Unknown);
        supply.update(false, 0.F, 0.F, false);
        EXPECT_FALSE(supply.state.valid);
    }
    {
        const hal::DevicePreferences device{0, 37, 3, 2};
        const auto restored = hal::DevicePreferences::decode(device.encode());
        EXPECT_TRUE(restored && restored->encode() == device.encode());
        EXPECT_FALSE(hal::DevicePreferences::decode(device.encode() & ~0xff00U));
        EXPECT_FALSE(hal::DevicePreferences::decode(device.encode() | 0x80000000U));
        EXPECT_TRUE(hal::kSleepDelayMs[0] == 0 && hal::kSleepDelayMs[3] == 600000);
        EXPECT_TRUE(hal::kSleepPowerOffDelayMs[2] == 300000);
        auto immediate = device;
        immediate.sleep_power_off_index = hal::kSleepPowerOffImmediate;
        EXPECT_TRUE(hal::DevicePreferences::decode(immediate.encode()).has_value());
    }
    using namespace vibescope;
    PlotRenderState stopped;
    stopped.serial = 900;
    stopped.generation = 7;
    stopped.first = 3600;
    stopped.count = 400;
    auto next = stopped;
    EXPECT_TRUE(next == stopped);
    next.first -= 100;
    EXPECT_FALSE(next == stopped); // A stopped pan must redraw.
    next = stopped;
    next.config.span_ms = 1000;
    EXPECT_FALSE(next == stopped);
    next = stopped;
    next.config.spectrum_db_scale = true;
    EXPECT_FALSE(next == stopped);
    next = stopped;
    next.config.spectrum_peak_count = 3;
    EXPECT_FALSE(next == stopped);
    next = stopped;
    next.config.waterfall_palette = WaterfallPalette::Viridis;
    EXPECT_FALSE(next == stopped);
    next = stopped;
    next.analysis_serial++;
    EXPECT_FALSE(next == stopped); // Late FFT result must redraw after stop.
    next = stopped;
    next.generation++;
    EXPECT_FALSE(next == stopped); // Same sample index in a new segment is new data.
    next = stopped;
    next.valid = true;
    EXPECT_FALSE(next == stopped);
    next = stopped;
    next.config.cursor_fraction = .25F;
    EXPECT_FALSE(next == stopped);
    next = stopped;
    next.axis = 1;
    EXPECT_FALSE(next == stopped);

    next = stopped;
    next.config.waterfall_window = 2;
    EXPECT_FALSE(next == stopped);
    EXPECT_TRUE(hal::battery_percent(6.F) == 0 && hal::battery_percent(8.23F) == 100);
    EXPECT_TRUE(hal::battery_percent(7.1F) == 49);
    hal::AutoChargePolicy charge;
    using hal::BatteryPresence;
    hal::PowerStatus pack;
    EXPECT_FALSE(charge.enable(pack, false, 0.F));
    pack.valid = true;
    pack.battery = BatteryPresence::Present;
    EXPECT_TRUE(charge.enable(pack, false, 0.F));
    for (unsigned i = 0; i < 29; ++i) EXPECT_TRUE(charge.enable(pack, true, 0.F));
    EXPECT_FALSE(charge.enable(pack, true, 0.F));
    EXPECT_TRUE(charge.enable(pack, true, -.4F));
    pack.valid = false;
    EXPECT_FALSE(charge.enable(pack, true, -.4F));

    // Retained history belongs in the rightmost 60-second slots; padding and
    // allocation guards must survive both empty history and clipped peaks.
    std::array<std::uint16_t, 2 + 8 * 4> pixels;
    pixels.fill(0xEEEE);
    std::vector<float> values;
    raster_scope_trend(pixels.data() + 1, 7, 4, 8, values, 1.F, 0x1111, 0x2222, false);
    EXPECT_TRUE(pixels.front() == 0xEEEE && pixels.back() == 0xEEEE);
    for (unsigned y = 0; y < 4; ++y) EXPECT_TRUE(pixels[1 + y * 8 + 7] == 0xEEEE);
    values = {2.F};
    raster_scope_trend(pixels.data() + 1, 7, 4, 8, values, 1.F, 0x1111, 0x2222, false);
    EXPECT_TRUE(pixels[1 + 6] == 0x2222); // Last sample at right edge, clipped to top.
    values = {0.F, -1.F, 0.F};
    raster_scope_trend(pixels.data() + 1, 7, 4, 8, values, 1.F, 0x1111, 0x2222, false, 0x2945, true, 7);
    EXPECT_TRUE(pixels[1 + 3 * 8 + 4] == 0x2222 && pixels[1 + 3 * 8 + 5] == 0x1111);
    values.assign(60, 0.F);
    raster_scope_trend(pixels.data() + 1, 7, 4, 8, values, 1.F, 0x1111, 0x2222, false);
    EXPECT_TRUE(pixels[1 + 3 * 8] == 0x2222);
    EXPECT_TRUE(pixels[1 + 3 * 8 + 6] == 0x2222);
    EXPECT_TRUE(pixels.front() == 0xEEEE && pixels.back() == 0xEEEE);
}
