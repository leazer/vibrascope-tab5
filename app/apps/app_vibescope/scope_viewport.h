/**
 * @file scope_viewport.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-04
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace vibescope {
// Half-open sample range. Coordinates always refer to original sample indices.
struct SampleViewport {
    std::size_t first{}, count{};
    void clamp(std::size_t total)
    {
        count = std::min(count, total);
        first = std::min(first, total - count);
    }
    void reset(std::size_t total, std::size_t requested)
    {
        count = std::min(total, std::max(std::size_t{1}, requested));
        first = total - count;
    }
    void zoom(std::size_t total, float factor)
    {
        clamp(total);
        if (!total || !std::isfinite(factor) || factor <= 0) return;
        const double centre = static_cast<double>(first) + static_cast<double>(count) / 2;
        count = static_cast<std::size_t>(std::clamp(static_cast<double>(count) * factor,
                                                   static_cast<double>(std::min(total, std::size_t{8})),
                                                   static_cast<double>(total)));
        first = static_cast<std::size_t>(std::max(0., centre - static_cast<double>(count) / 2));
        clamp(total);
    }
    void pan(std::size_t total, std::int64_t delta)
    {
        clamp(total);
        first = static_cast<std::size_t>(std::clamp(static_cast<std::int64_t>(first) + delta,
                                                  std::int64_t{0}, static_cast<std::int64_t>(total - count)));
    }
};

// Sparse: every sample. Dense: extrema per horizontal pixel, in time order.
// Always retain endpoints; do not invent values or stretch extrema to bin edges.
template <class Value, class Emit>
void visit_waveform(std::size_t first, std::size_t count, std::size_t width, Value value, Emit emit)
{
    if (!count || !width) return;
    if (count <= width) {
        for (std::size_t i = first; i < first + count; ++i) emit(i, value(i));
        return;
    }
    emit(first, value(first));
    for (std::size_t column = 0; column < width; ++column) {
        const auto begin = first + column * count / width;
        const auto end = first + (column + 1) * count / width;
        auto low = begin, high = begin;
        for (auto i = begin + 1; i < end; ++i) {
            if (value(i) < value(low)) low = i;
            if (value(i) > value(high)) high = i;
        }
        if (low > high) std::swap(low, high);
        emit(low, value(low));
        if (high != low) emit(high, value(high));
    }
    emit(first + count - 1, value(first + count - 1));
}
struct SpectrumGrid {
    std::size_t count{};
    float bin_hz{};
    void sync(SampleViewport& view, std::size_t bins, float resolution)
    {
        if (count != bins || bin_hz != resolution) view.reset(bins, bins);
        count = bins;
        bin_hz = resolution;
    }
};

inline SampleViewport spectrum_band(std::size_t bins, float bin_hz, unsigned low_hz, unsigned high_hz)
{
    if (bins < 2 || !std::isfinite(bin_hz) || bin_hz <= 0) return {0, bins};
    const auto last = static_cast<double>(bins - 1);
    const auto first = static_cast<std::size_t>(std::clamp(std::ceil(low_hz / double(bin_hz)), 0., last - 1));
    const auto end = high_hz
        ? static_cast<std::size_t>(std::clamp(std::floor(high_hz / double(bin_hz)), double(first + 1), last))
        : bins - 1;
    return {first, end - first + 1};
}

// Sample a fixed frequency column from an FFT whose bin spacing can drift.
template <class Spectrum>
float waterfall_column(const Spectrum& spectrum, float bin_hz, float center_hz, float half_width_hz)
{
    const auto last = spectrum.size() - 1;
    const float center = std::clamp(center_hz / bin_hz, 0.F, static_cast<float>(last));
    const float radius = half_width_hz / bin_hz;
    if (radius < .5F) {
        const auto left = static_cast<std::size_t>(center);
        const auto right = std::min(left + 1, last);
        return spectrum[left] + (spectrum[right] - spectrum[left]) * (center - left);
    }
    const auto first = static_cast<std::size_t>(std::ceil(std::max(0.F, center - radius)));
    const auto end = static_cast<std::size_t>(std::floor(std::min(static_cast<float>(last), center + radius)));
    float peak = 0.F;
    for (auto i = first; i <= end; ++i) peak = std::max(peak, spectrum[i]);
    return peak;
}

inline float spectrum_db(float amplitude)
{
    return 20.F * std::log10(std::max(amplitude, 1e-8F));
}

// Disjoint half-open groups: a bin on the boundary belongs to one column only.
template <class Value, class Emit>
void visit_spectrum(std::size_t first, std::size_t count, std::size_t width, Value value, Emit emit)
{
    if (!count || !width) return;
    if (count <= width) {
        for (std::size_t i = first; i < first + count; ++i) emit(i, value(i));
        return;
    }
    auto previous = first;
    emit(first, value(first));
    for (std::size_t column = 0; column < width; ++column) {
        const auto begin = first + column * count / width, end = first + (column + 1) * count / width;
        auto peak = begin;
        for (auto i = begin + 1; i < end; ++i) if (value(i) > value(peak)) peak = i;
        if (peak != previous) emit(peak, value(peak));
        previous = peak;
    }
    if (previous != first + count - 1) emit(first + count - 1, value(first + count - 1));
}
}  // namespace vibescope
