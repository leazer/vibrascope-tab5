/**
 * @file scope_preferences.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-08
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include "scope_analysis.h"
#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>

namespace vibescope {
inline constexpr std::array<unsigned, 3> kStatisticsWindowsMs{500, 1000, 2000};
inline constexpr std::array<unsigned, 4> kWaveformWindowsMs{100, 200, 1000, 2000};
inline constexpr std::array<unsigned, 5> kRecordDurationsSeconds{0, 60, 300, 600, 1800};

inline constexpr std::array<unsigned, 5> kRmsTrendWindowsSeconds{15, 30, 60, 120, 300};
inline constexpr unsigned kDefaultRmsTrendWindow = 2;

struct ScopePreferences {
    unsigned source{}, adxl_range{}, icm_range{3}, statistics_ms{1000}, trigger_centi_g{150};
    unsigned duration_index{}, layout{2};
    bool sidebar_visible{true};
    unsigned trigger_position{50};
    bool resultant_display{};
    std::array<PlotConfig, 3> plots{};
    unsigned rms_trend_window{kDefaultRmsTrendWindow};

    // The stored format is a fixed sequence of little-endian uint32 values.
    // Plot source follows the global source, and cursor positions are transient.
    using Values = std::array<std::uint32_t, 26>;
    Values values() const
    {
        Values values{source, adxl_range, icm_range, statistics_ms, trigger_centi_g,
                      duration_index, layout, (sidebar_visible ? 1U : 0U) | (((trigger_position / 25U + 3U) % 5U) << 1U) |
                          (resultant_display ? 16U : 0U) | ((rms_trend_window + 1U) << 5U)};
        unsigned index = 8;
        for (const auto& plot : plots) {
            values[index++] = static_cast<unsigned>(plot.kind);
            values[index++] = plot.span_ms;
            values[index++] = plot.axis;
            values[index++] = (plot.auto_scale ? 1U : 0U) | (plot.cursor ? 2U : 0U) |
                              (plot.all_axes ? 4U : 0U) | (plot.spectrum_db_scale ? 8U : 0U) |
                              (plot.dual_cursor ? 16U : 0U) | (((plot.cursor_axis + 1U) % 3U) << 5U) |
                              ((static_cast<unsigned>(plot.axis_mode) + 1U) << 7U) |
                              ((plot.spectrum_peak_count + 1U) << 10U) |
                              (static_cast<unsigned>(plot.waterfall_palette) << 13U) |
                              (plot.waterfall_window << 15U);
        }
        for (const auto& plot : plots) {
            values[index++] = plot.spectrum_low_hz;
            values[index++] = plot.spectrum_high_hz;
        }
        return values;
    }

    static std::optional<ScopePreferences> decode(const Values& values)
    {
        const auto trend_window = values[7] >> 5U;
        if (values[0] > 1 || values[1] >= kAdxlRangesG.size() || values[2] >= kIcmRangesG.size() ||
            std::find(kStatisticsWindowsMs.begin(), kStatisticsWindowsMs.end(), values[3]) == kStatisticsWindowsMs.end() ||
            values[4] < 5 || values[4] > 1000 || values[5] >= kRecordDurationsSeconds.size() ||
            values[6] < 1 || values[6] > 3 || (values[7] & 15U) >= 10 || trend_window > kRmsTrendWindowsSeconds.size()) return std::nullopt;
        ScopePreferences prefs{values[0], values[1], values[2], values[3], values[4], values[5], values[6],
                               (values[7] & 1U) != 0, ((((values[7] & 15U) >> 1U) + 2U) % 5U) * 25U,
                               (values[7] & 16U) != 0, {}, trend_window ? trend_window - 1U : kDefaultRmsTrendWindow};
        unsigned index = 8;
        for (auto& plot : prefs.plots) {
            const auto kind = values[index++];
            const auto span = values[index++];
            const auto axis = values[index++];
            const auto flags = values[index++];
            const auto mode = (flags >> 7U) & 7U;
            const auto peaks = (flags >> 10U) & 7U;
            const auto palette = (flags >> 13U) & 3U;
            const auto waterfall_window = (flags >> 15U) & 7U;
            if (kind > static_cast<unsigned>(PlotKind::Gyro) || axis > 2 || (flags & 96U) == 96U || mode > 5 ||
                (flags >> 18U) || peaks > kMaxSpectrumPeaks + 1U ||
                waterfall_window >= kWaterfallWindowsSeconds.size() ||
                palette > static_cast<unsigned>(WaterfallPalette::Viridis) ||
                std::find(kWaveformWindowsMs.begin(), kWaveformWindowsMs.end(), span) == kWaveformWindowsMs.end())
                return std::nullopt;
            plot.kind = static_cast<PlotKind>(kind);
            plot.span_ms = span;
            plot.axis = axis;
            plot.auto_scale = (flags & 1U) != 0;
            plot.cursor = (flags & 2U) != 0;
            plot.all_axes = (flags & 4U) != 0;
            plot.spectrum_db_scale = (flags & 8U) != 0;
            plot.dual_cursor = (flags & 16U) != 0;
            plot.cursor_axis = (((flags >> 5U) & 3U) + 2U) % 3U;
            // Zero is reserved for preference files written before peak labels.
            plot.spectrum_peak_count = peaks ? peaks - 1U : kDefaultSpectrumPeaks;
            plot.waterfall_palette = static_cast<WaterfallPalette>(palette);
            plot.waterfall_window = waterfall_window;
            if (mode) plot.axis_mode = static_cast<PlotAxisMode>(mode - 1U);
            else if (!plot.all_axes || (plot.kind == PlotKind::Waterfall && axis != 2)) {
                constexpr PlotAxisMode previous[] = {PlotAxisMode::X, PlotAxisMode::Y, PlotAxisMode::Resultant};
                plot.axis_mode = previous[axis];
            }
        }
        for (auto& plot : prefs.plots) {
            plot.spectrum_low_hz = values[index++];
            plot.spectrum_high_hz = values[index++];
            if (plot.spectrum_low_hz > 8000 || plot.spectrum_high_hz > 8000 ||
                (plot.spectrum_high_hz && plot.spectrum_low_hz >= plot.spectrum_high_hz)) return std::nullopt;
        }
        return prefs;
    }
};

namespace preference_file {
using Words = std::array<std::uint32_t, ScopePreferences::Values{}.size() + 4>;
inline constexpr std::uint32_t kMagic = 0x46434e56; // VNCF
inline constexpr std::uint32_t kVersion = 2;

inline std::uint32_t checksum(const Words& words, std::size_t count)
{
    std::uint32_t hash = 2166136261U;
    for (std::size_t i = 0; i < count; ++i)
        for (unsigned shift = 0; shift < 32; shift += 8)
            hash = (hash ^ ((words[i] >> shift) & 0xffU)) * 16777619U;
    return hash;
}

struct Slot {
    std::optional<ScopePreferences> prefs;
    std::uint32_t generation{};
};

inline FILE* open(const std::string& path, const char* mode)
{
#if defined(_MSC_VER)
    FILE* file = nullptr;
    return fopen_s(&file, path.c_str(), mode) == 0 ? file : nullptr;
#else
    return std::fopen(path.c_str(), mode);
#endif
}

inline Slot read(const std::string& path)
{
    auto* file = open(path, "rb");
    if (!file) return {};
    Words words{};
    const auto bytes = std::fread(words.data(), 1, sizeof(words), file);
    const auto count = bytes / sizeof(words[0]);
    const bool complete = bytes % sizeof(words[0]) == 0 && std::fgetc(file) == EOF && !std::ferror(file);
    std::fclose(file);
    const bool legacy = words[1] == 1 && count == 24;
    if (!complete || words[0] != kMagic || (!legacy && (words[1] != kVersion || count != words.size())) ||
        words[count - 1] != checksum(words, count - 1)) return {};
    ScopePreferences::Values values{};
    std::copy_n(words.begin() + 3, count - 4, values.begin());
    auto prefs = ScopePreferences::decode(values);
    return {prefs, prefs ? words[2] : 0};
}

inline bool write(const std::string& path, const ScopePreferences& prefs, std::uint32_t generation)
{
    Words words{kMagic, kVersion, generation};
    const auto values = prefs.values();
    std::copy(values.begin(), values.end(), words.begin() + 3);
    words.back() = checksum(words, words.size() - 1);
    auto* file = open(path, "wb");
    if (!file) return false;
    const bool written = std::fwrite(words.data(), sizeof(words[0]), words.size(), file) == words.size();
    return std::fclose(file) == 0 && written;
}
}  // namespace preference_file
}  // namespace vibescope
