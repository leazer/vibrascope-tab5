/**
 * @file scope_raster.h
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
#include <cstdlib>
#include <vector>
#include "scope_viewport.h"

namespace vibescope {
using ScopePlotPoints = std::array<std::array<std::int32_t, 256>, 3>;

inline void scope_raster_line(std::uint16_t* pixels, int height, int stride, int x, int y, int end_x, int end_y,
                              std::uint16_t color)
{
    const int dx = std::abs(end_x - x), sx = x < end_x ? 1 : -1;
    const int dy = -std::abs(end_y - y), sy = y < end_y ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        pixels[y * stride + x] = color;
        if (y + 1 < height) pixels[(y + 1) * stride + x] = color;
        if (x == end_x && y == end_y) break;
        const int twice = error * 2;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
}

// Opaque RGB565 plot, with an explicit padded stride for LVGL draw buffers.
// Large plots become one image blit instead of hundreds of styled draw tasks.
inline void raster_scope_plot(std::uint16_t* pixels, int width, int height, int stride, const ScopePlotPoints& points,
                              bool positive, std::int32_t missing, std::uint16_t background, std::uint16_t grid,
                              const std::array<std::uint16_t, 3>& colors)
{
    if (!pixels || width <= 0 || height <= 0 || stride < width) return;
    std::fill(pixels, pixels + stride * height, background);
    for (int division = 0; division < 5; ++division) {
        const int x = division * (width - 1) / 4, y = division * (height - 1) / 4;
        for (int row = 0; row < height; ++row) pixels[row * stride + x] = grid;
        std::fill(pixels + y * stride, pixels + y * stride + width, grid);
    }
    for (unsigned axis = 0; axis < 3; ++axis) {
        bool previous = false;
        int last_x = 0, last_y = 0;
        for (unsigned i = 0; i < 256; ++i) {
            if (points[axis][i] == missing) {
                previous = false;
                continue;
            }
            const int value = static_cast<int>(std::clamp<std::int32_t>(points[axis][i], positive ? 0 : -1000, 1000));
            const int x     = static_cast<int>(i) * (width - 1) / 255;
            const int y     = (1000 - value) * (height - 1) / (positive ? 1000 : 2000);
            scope_raster_line(pixels, height, stride, previous ? last_x : x, previous ? last_y : y, x, y, colors[axis]);
            previous = true;
            last_x   = x;
            last_y   = y;
        }
    }
}

template <class Samples>
void raster_scope_time(std::uint16_t* pixels, int width, int height, int stride, const Samples& samples,
                       const SampleViewport& view, const std::array<float, 3>& means, float maximum,
                       bool all_axes, unsigned selected_axis, const std::array<std::uint16_t, 3>& colors)
{
    if (!pixels || width <= 0 || height <= 0 || stride < width || !view.count || maximum <= 0) return;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!all_axes && axis != selected_axis) continue;
        bool previous = false;
        int last_x = 0, last_y = 0;
        visit_waveform(view.first, view.count, static_cast<std::size_t>(width),
            [&](std::size_t i) { return samples[i][axis]; },
            [&](std::size_t i, float value) {
                const int x = view.count > 1 ? static_cast<int>((i - view.first) * (width - 1) / (view.count - 1)) : 0;
                const float normalized = std::clamp((value - means[axis]) / maximum, -1.F, 1.F);
                const int y = static_cast<int>((1.F - normalized) * .5F * (height - 1));
                scope_raster_line(pixels, height, stride, previous ? last_x : x, previous ? last_y : y, x, y, colors[axis]);
                previous = true;
                last_x = x;
                last_y = y;
            });
    }
}
inline void raster_scope_resultant(std::uint16_t* pixels, int width, int height, int stride,
                                  const std::vector<float>& samples, const SampleViewport& view,
                                  float maximum, std::uint16_t color)
{
    if (!pixels || width <= 0 || height <= 0 || stride < width || !view.count || maximum <= 0) return;
    bool previous = false;
    int last_x = 0, last_y = 0;
    visit_waveform(view.first, view.count, static_cast<std::size_t>(width),
        [&](std::size_t i) { return samples[i]; },
        [&](std::size_t i, float value) {
            const int x = view.count > 1 ? static_cast<int>((i - view.first) * (width - 1) / (view.count - 1)) : 0;
            const int y = static_cast<int>((1.F - std::clamp(value / maximum, 0.F, 1.F)) * (height - 1));
            scope_raster_line(pixels, height, stride, previous ? last_x : x, previous ? last_y : y, x, y, color);
            previous = true;
            last_x = x;
            last_y = y;
        });
}

template <class Spectra>
void raster_scope_spectrum(std::uint16_t* pixels, int width, int height, int stride, const Spectra& spectra,
                           const SampleViewport& view, float minimum, float maximum, bool db,
                           bool all_axes, unsigned selected_axis, const std::array<std::uint16_t, 3>& colors)
{
    if (!pixels || width <= 0 || height <= 0 || stride < width || !view.count || maximum <= minimum) return;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!all_axes && axis != selected_axis) continue;
        const auto& bins = spectra[axis].spectrum;
        if (view.first > bins.size() || view.count > bins.size() - view.first) continue;
        bool previous = false;
        int last_x = 0, last_y = 0;
        visit_spectrum(view.first, view.count, static_cast<std::size_t>(width),
            [&](std::size_t i) { return bins[i]; },
            [&](std::size_t i, float value) {
                const int x = view.count > 1 ? static_cast<int>((i - view.first) * (width - 1) / (view.count - 1)) : 0;
                const float amplitude = db ? spectrum_db(value) : value;
                const float normalized = std::clamp((amplitude - minimum) / (maximum - minimum), 0.F, 1.F);
                const int y = static_cast<int>((1.F - normalized) * (height - 1));
                scope_raster_line(pixels, height, stride, previous ? last_x : x, previous ? last_y : y, x, y, colors[axis]);
                previous = true;
                last_x = x;
                last_y = y;
            });
    }
}
// The newest one-second slot stays at the right edge. Missing samples break
// the trace, and row padding belongs to the allocation, not the image.
template<class Values>
void raster_scope_trend(std::uint16_t* pixels, int width, int height, int stride,
                        const Values& values, float maximum, std::uint16_t background,
                        std::uint16_t color, bool grid, std::uint16_t grid_color = 0x2945,
                        bool clear = true, std::size_t slots = 60)
{
    if (!pixels || width <= 0 || height <= 0 || stride < width) return;
    if (clear)
        for (int y = 0; y < height; ++y) std::fill_n(pixels + y * stride, width, background);
    if (grid)
        for (int i = 0; i <= 4; ++i) {
            const int y = i * (height - 1) / 4;
            std::fill_n(pixels + y * stride, width, grid_color);
        }
    maximum = std::max(maximum, .001F);
    slots = std::max<std::size_t>(2, slots);
    const auto count = std::min(slots, values.size());
    bool previous = false;
    int last_x = 0, last_y = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const float value = values[values.size() - count + i];
        if (!std::isfinite(value) || value < 0.F) { previous = false; continue; }
        const int x = static_cast<int>((slots - count + i) * (width - 1) / (slots - 1));
        const int y = static_cast<int>((1.F - std::clamp(value / maximum, 0.F, 1.F)) * (height - 1));
        scope_raster_line(pixels, height, stride, previous ? last_x : x, previous ? last_y : y, x, y, color);
        previous = true;
        last_x = x;
        last_y = y;
    }
}
}  // namespace vibescope
