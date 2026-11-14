/**
 * @file scope_analysis.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <vhm/probe/messages.hpp>

namespace vibescope {

using Axes = std::array<float, 3>;
enum class PlotKind : std::uint8_t { Time, Spectrum, Waterfall, Gyro };
enum class Source : std::uint8_t { Adxl, Icm, Gyro };
enum class PlotAxisMode : std::uint8_t { Follow, XYZ, X, Y, Resultant };
inline constexpr std::array<unsigned, 5> kWaterfallWindowsSeconds{3, 5, 10, 20, 30};
enum class WaterfallPalette : std::uint8_t { Amber, BlueRed, Viridis };
inline constexpr unsigned kMaxSpectrumPeaks = 3;
inline constexpr unsigned kDefaultSpectrumPeaks = 1;

struct PlotConfig {
    PlotKind kind{PlotKind::Time};
    Source source{Source::Adxl};
    std::uint32_t span_ms{100};
    bool auto_scale{true}, cursor{false}, all_axes{true};
    bool spectrum_db_scale{false};
    float cursor_fraction{.5F};
    bool dual_cursor{};
    float cursor_b_fraction{.75F};
    unsigned axis{2};
    unsigned cursor_axis{2};
    PlotAxisMode axis_mode{PlotAxisMode::Follow};
    unsigned spectrum_low_hz{}, spectrum_high_hz{}; // Zero high selects Nyquist.
    unsigned spectrum_peak_count{kDefaultSpectrumPeaks};
    WaterfallPalette waterfall_palette{WaterfallPalette::Amber};
    unsigned waterfall_window{};

    void normalize_source()
    {
        if (kind == PlotKind::Gyro) source = Source::Gyro;
        else if (source == Source::Gyro) source = Source::Icm;
    }
};

inline constexpr std::array<unsigned, 5> kRmsBandEdgesHz{5, 20, 100, 500, 1000};
inline constexpr auto kRmsBandCount = kRmsBandEdgesHz.size() - 1;
inline constexpr std::size_t kScopeFftLimit = 2048;
// Band zoom raises the FFT size (real window when history allows) so a narrow
// spectrum/waterfall band still draws a dense curve instead of a few bins.
inline constexpr std::size_t kScopeFftZoomLimit = 16384;
inline constexpr std::array<unsigned, 3> kAdxlRangesG{10, 20, 40};
inline constexpr std::array<unsigned, 4> kIcmRangesG{2, 4, 8, 16};

struct Analysis {
    bool valid{false};
    float rms{}, peak{}, peak_to_peak{}, crest{}, kurtosis{}, frequency{}, bin_hz{};
    std::vector<float> spectrum;
    std::array<float, kRmsBandCount> band_rms{};
    std::array<bool, kRmsBandCount> band_valid{};
    std::size_t sample_count{};
};

struct SpectrumPeak {
    std::size_t bin{}; // Zero is DC and means no label.
    unsigned axis{};
    float amplitude{};
};
std::array<SpectrumPeak, kMaxSpectrumPeaks> spectrum_peaks(const std::array<const Analysis*, 3>& spectra,
                                                          std::size_t first, std::size_t count, unsigned limit);

// Statistics are AC quantities (mean removed), over the actual available window.
// Spectrum: symmetric Hann, coherent-gain corrected one-sided peak amplitude in
// input units (g or dps); DC is hidden, Nyquist is not doubled. FFT uses the last
// power-of-two window, capped at 2048 by default; spectrum_bins above that zooms
// the window up to kScopeFftZoomLimit (bounded by available samples), refining
// bin_hz for dense narrow-band plots. bin_hz is the un-interpolated resolution.
// Allocation failure can leave valid statistics with an empty spectrum.
Analysis analyze(const std::vector<Axes>& samples, float rate_hz, std::size_t axis, std::size_t stats_count = 0,
                 std::size_t spectrum_bins = 0);
Analysis analyze_resultant(const std::vector<Axes>& samples, std::size_t stats_count,
                           const std::array<Analysis, 3>& axes, std::vector<float>* waveform);
void resultant_waveform(const std::vector<Axes>& samples, const Axes& mean, std::vector<float>& values);
float convert_accel(std::int32_t count, std::size_t axis, bool adxl, const vhm::probe::CalibrationRecord& calibration,
                    unsigned range_g = 0);
float convert_gyro(std::int32_t count, std::size_t axis, const vhm::probe::CalibrationRecord& calibration);
// Live monitoring retains near-full-scale samples, unlike strict diagnostic
// selection. Integrity and timing faults remain unusable.
bool scope_can_display(vhm::Quality quality);

class SampleHistory {
public:
    static constexpr std::size_t kCapacity = 16384;
    void push(Axes sample, std::uint32_t segment, std::uint32_t config, std::uint64_t sequence,
              bool near_full_scale = false);
    bool near_full_scale(std::size_t count) const;
    void clear();
    std::vector<Axes> copy(std::size_t count) const;
    std::size_t size() const
    {
        return _size;
    }
    std::uint64_t generation() const
    {
        return _generation;
    }

private:
    std::array<Axes, kCapacity> _samples{};
    std::size_t _head{}, _size{};
    std::uint32_t _segment{}, _config{};
    std::uint64_t _next{};
    std::uint64_t _generation{};
    std::uint64_t _last_near_full_scale{};
    bool _have_near_full_scale{};
};
}  // namespace vibescope
