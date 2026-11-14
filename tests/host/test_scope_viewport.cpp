/**
 * @file test_scope_viewport.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-04
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "test_support.hpp"
#include "apps/app_vibescope/scope_viewport.h"
#include "apps/app_vibescope/scope_raster.h"
#include <array>
#include <vector>
#include <utility>

void run_scope_viewport_tests()
{
    using namespace vibescope;
    SampleViewport view;
    view.reset(800, 400);
    EXPECT_TRUE(view.first == 400 && view.count == 400);
    view.zoom(800, .5F);
    EXPECT_TRUE(view.first == 500 && view.count == 200);
    view.pan(800, -10000);
    EXPECT_TRUE(view.first == 0);
    view.pan(800, 10000);
    EXPECT_TRUE(view.first == 600);
    view.zoom(800, 100.F);
    EXPECT_TRUE(view.first == 0 && view.count == 800);
    view.reset(0, 400);
    view.zoom(0, .5F);
    view.pan(0, -100);
    EXPECT_TRUE(view.count == 0 && view.first == 0);
    view.reset(3, 400);
    view.zoom(3, .5F);
    EXPECT_TRUE(view.count == 3 && view.first == 0);
    view.reset(8000, 800);
    for (int i = 0; i < 20; ++i) view.zoom(8000, .5F);
    EXPECT_TRUE(view.count == 8 && view.first + view.count <= 8000);
    view.zoom(8000, 0.F);
    EXPECT_TRUE(view.count == 8);

    const auto full_band = spectrum_band(1025, 2.F, 0, 0);
    EXPECT_TRUE(full_band.first == 0 && full_band.count == 1025);
    const auto selected_band = spectrum_band(1025, 2.F, 101, 200);
    EXPECT_TRUE(selected_band.first == 51 && selected_band.count == 50);
    const auto clipped_band = spectrum_band(257, 2.F, 1000, 2000);
    EXPECT_TRUE(clipped_band.first == 255 && clipped_band.count == 2);

    const std::array<float, 7> amplitudes{0, 1, 3, 9, 4, 2, 1};
    // A fixed physical-frequency column uses the current FFT grid for each row.
    EXPECT_TRUE(waterfall_column(amplitudes, 2.F, 6.F, 3.F) == 9.F);
    EXPECT_TRUE(waterfall_column(amplitudes, 2.1F, 6.F, 3.F) == 9.F);
    EXPECT_TRUE(waterfall_column(amplitudes, 2.F, 5.F, .5F) == 6.F);
    EXPECT_TRUE(waterfall_column(amplitudes, 2.F, 15.F, .5F) == 1.F);

    std::vector<float> samples{0, 4, -3, 1, 8, -7, 2};
    std::vector<std::pair<std::size_t, float>> points;
    auto emit = [&](std::size_t i, float v) { points.emplace_back(i, v); };
    auto value = [&](std::size_t i) { return samples[i]; };
    visit_waveform(0, samples.size(), 100, value, emit);
    EXPECT_TRUE(points.size() == samples.size());
    for (std::size_t i = 0; i < points.size(); ++i) EXPECT_TRUE(points[i].first == i);
    points.clear();
    visit_waveform(0, samples.size(), 2, value, emit);
    bool positive = false, negative = false;
    for (std::size_t i = 0; i < points.size(); ++i) {
        positive |= points[i].second == 8;
        negative |= points[i].second == -7;
        if (i) EXPECT_TRUE(points[i].first >= points[i - 1].first);
    }
    EXPECT_TRUE(positive && negative);
    EXPECT_TRUE(points.front().first == 0 && points.back().first == samples.size() - 1);
    points.clear();
    visit_waveform(2, 3, 100, value, emit);
    EXPECT_TRUE(points.size() == 3 && points.front().first == 2 && points.back().first == 4);
    points.clear();
    visit_waveform(0, 0, 0, value, emit);
    EXPECT_TRUE(points.empty());

    // Nonzero viewport origin, clipping and padded stride must not write outside the image.
    std::vector<std::array<float, 3>> axes(20);
    axes[10][0] = 10.F;
    axes[12][0] = -10.F;
    std::vector<std::uint16_t> pixels(8 * 9 + 2, 0xEEEE);
    view.first = 8;
    view.count = 8;
    const std::array<float, 3> means{};
    const std::array<std::uint16_t, 3> colors{0x1234, 0x5678, 0xABCD};
    raster_scope_time(pixels.data() + 1, 7, 9, 8, axes, view, means, 1.F, false, 0, colors);
    EXPECT_TRUE(pixels.front() == 0xEEEE && pixels.back() == 0xEEEE);
    bool top = false, bottom = false;
    for (int x = 0; x < 7; ++x) {
        top |= pixels[1 + x] == colors[0];
        bottom |= pixels[1 + 8 * 8 + x] == colors[0];
    }
    EXPECT_TRUE(top && bottom);
    for (int y = 0; y < 9; ++y) EXPECT_TRUE(pixels[1 + y * 8 + 7] == 0xEEEE);
}
