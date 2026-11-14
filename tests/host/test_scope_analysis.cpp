/**
 * @file test_scope_analysis.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "test_support.hpp"
#include "apps/app_vibescope/scope_analysis.h"
#include "apps/app_vibescope/scope_analysis_worker.h"
#include "apps/app_vibescope/scope_preferences.h"
#include "apps/app_vibescope/scope_raster.h"
#include <cmath>
#include <future>
#include <chrono>
#include <thread>
#include <atomic>
#include <limits>

void run_scope_analysis_tests()
{
    using namespace vibescope;
    // Display retains two seconds; analysis can select the latest one second.
    SampleHistory two_seconds;
    for (std::uint64_t i = 0; i < 20000; ++i)
        two_seconds.push({static_cast<float>(i), 0, 0}, 1, 1, i);
    const auto display_window = two_seconds.copy(16000);
    const auto analysis_window = two_seconds.copy(8000);
    EXPECT_TRUE(display_window.size() == 16000);
    EXPECT_TRUE(display_window.front()[0] == 4000 && display_window.back()[0] == 19999);
    EXPECT_TRUE(analysis_window.size() == 8000 && analysis_window.front()[0] == 12000);
    // Near full-scale is a warning on measured values, not a reason to erase
    // a live waveform. Timing/integrity faults must still reject the batch.
    EXPECT_TRUE(scope_can_display(vhm::Quality::Saturated));
    EXPECT_TRUE(scope_can_display(vhm::Quality::Saturated | vhm::Quality::SegmentStart));
    for (auto fatal : {vhm::Quality::TimeUncertain, vhm::Quality::SequenceGap, vhm::Quality::CrcError,
                       vhm::Quality::FormatError, vhm::Quality::FifoOverflow, vhm::Quality::QueueOverflow})
        EXPECT_FALSE(scope_can_display(vhm::Quality::Saturated | fatal));
    SampleHistory warned;
    warned.push({1, 0, 0}, 1, 1, 0);
    const auto continuous = warned.generation();
    warned.push({9.6F, 0, 0}, 1, 1, 1, true);
    warned.push({2, 0, 0}, 1, 1, 2);
    EXPECT_TRUE(warned.generation() == continuous && warned.size() == 3);
    EXPECT_TRUE(warned.copy(3)[1][0] == 9.6F);
    EXPECT_TRUE(warned.near_full_scale(2));
    EXPECT_FALSE(warned.near_full_scale(1));
    EXPECT_FALSE(warned.near_full_scale(0));
    warned.push({3, 0, 0}, 2, 1, 0);
    EXPECT_FALSE(warned.near_full_scale(100));  // a new segment cannot inherit the warning
    warned.push({4, 0, 0}, 2, 1, 1, true);
    warned.clear();
    EXPECT_FALSE(warned.near_full_scale(100));

    std::vector<Axes> samples(1024);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const float v = static_cast<float>(std::sin(2.0 * 3.141592653589793 * 64 * i / 1024));
        samples[i]    = {v + 2, v * 2, v * .5F};
    }
    const auto result = analyze(samples, 1024, 0);
    EXPECT_TRUE(result.valid);
    EXPECT_TRUE(std::abs(result.rms - std::sqrt(.5F)) < .002F);
    EXPECT_TRUE(std::abs(result.peak - 1.F) < .002F);
    EXPECT_TRUE(std::abs(result.frequency - 64.F) < .1F);
    EXPECT_TRUE(std::abs(result.spectrum[64] - 1.F) < .01F);
    EXPECT_TRUE(std::abs(result.kurtosis - 1.5F) < .01F);

    // Band zoom: a raised target extends the window over the longer history
    // (no zero padding beyond available samples), refining bin_hz eight-fold
    // while the tone keeps its coherent gain. The default cap stays at 2048.
    std::vector<Axes> zoom_samples(8192);
    for (std::size_t i = 0; i < zoom_samples.size(); ++i) {
        const float v = static_cast<float>(std::sin(2.0 * 3.141592653589793 * 64 * i / 1024));
        zoom_samples[i] = {v + 2, 0, 0};
    }
    EXPECT_TRUE(analyze(zoom_samples, 1024, 0).spectrum.size() == 1025);
    const auto zoomed = analyze(zoom_samples, 1024, 0, 0, 8192);
    EXPECT_TRUE(zoomed.valid && zoomed.spectrum.size() == 4097);
    EXPECT_TRUE(std::abs(zoomed.bin_hz - 1024.F / 8192.F) < 1e-4F);
    EXPECT_TRUE(std::abs(zoomed.spectrum[512] - 1.F) < .01F);

    // A rotating vector has constant magnitude, while each axis still has a
    // 64 Hz tone. Resultant power must retain the vibration and its frequency.
    std::vector<Axes> rotating(2048);
    for (std::size_t i = 0; i < rotating.size(); ++i) {
        const double phase = 2.0 * 3.141592653589793 * 64 * i / 2048;
        rotating[i] = {2.F + .25F * static_cast<float>(std::cos(phase)),
                       .25F * static_cast<float>(std::sin(phase)), -1.F};
    }
    const std::array<Analysis, 3> rotating_axes{analyze(rotating, 2048, 0), analyze(rotating, 2048, 1),
                                               analyze(rotating, 2048, 2)};
    std::vector<float> magnitude;
    const auto combined = analyze_resultant(rotating, 2048, rotating_axes, &magnitude);
    EXPECT_TRUE(combined.valid && magnitude.size() == rotating.size());
    EXPECT_TRUE(std::abs(combined.rms - .25F) < .001F && std::abs(combined.peak - .25F) < .001F);
    EXPECT_TRUE(std::abs(magnitude[0] - .25F) < .001F && std::abs(magnitude[8] - .25F) < .001F);
    EXPECT_TRUE(combined.frequency == 64.F && std::abs(combined.spectrum[64] - .25F * std::sqrt(2.F)) < .001F);
    EXPECT_TRUE(combined.band_valid[1] && std::abs(combined.band_rms[1] - .25F) < .001F);

    ScopePreferences prefs;
    prefs.resultant_display = true;
    prefs.trigger_position = 75;
    prefs.plots[0].axis_mode = PlotAxisMode::XYZ;
    prefs.plots[1].axis_mode = PlotAxisMode::Resultant;
    prefs.plots[2].cursor_axis = 1;
    prefs.rms_trend_window = 4;
    const auto restored = ScopePreferences::decode(prefs.values());
    EXPECT_TRUE(restored && restored->values() == prefs.values());
    EXPECT_TRUE(restored && kRmsTrendWindowsSeconds[restored->rms_trend_window] == 300);
    auto invalid_trend = prefs.values();
    invalid_trend[7] |= 7U << 5U;
    EXPECT_FALSE(ScopePreferences::decode(invalid_trend).has_value());
    auto legacy = prefs.values();
    legacy[7] &= 31U;
    for (unsigned index : {11U, 15U, 19U}) legacy[index] &= 0x3ffU;
    const auto previous = ScopePreferences::decode(legacy);
    EXPECT_TRUE(previous && previous->rms_trend_window == kDefaultRmsTrendWindow &&
                previous->plots[1].spectrum_peak_count == 1 &&
                previous->plots[2].waterfall_palette == WaterfallPalette::Amber &&
                previous->plots[2].waterfall_window == 0);
    prefs.plots[1].spectrum_peak_count = 0;
    prefs.plots[2].spectrum_peak_count = 3;
    prefs.plots[2].waterfall_palette = WaterfallPalette::BlueRed;
    prefs.plots[2].waterfall_window = 4;
    const auto display = ScopePreferences::decode(prefs.values());
    EXPECT_TRUE(display && display->values() == prefs.values() && display->plots[1].spectrum_peak_count == 0);

    // Independent tones in all four bands catch bin routing and Hann power
    // normalization errors. A static 1 g offset must not enter any band.
    std::vector<Axes> band_tones(2048);
    for (std::size_t i = 0; i < band_tones.size(); ++i) {
        const double phase = 2 * 3.141592653589793 * i / 2048;
        band_tones[i][0] = static_cast<float>(1 + .125 * std::sin(12 * phase) +
            .25 * std::sin(64 * phase) + .5 * std::sin(250 * phase) + std::sin(750 * phase));
    }
    const auto bands = analyze(band_tones, 2048, 0);
    const float expected_rms[] = {.088388F, .176777F, .353553F, .707107F};
    for (unsigned band = 0; band < 4; ++band) {
        EXPECT_TRUE(bands.band_valid[band]);
        EXPECT_TRUE(std::abs(bands.band_rms[band] - expected_rms[band]) < .001F);
    }
    const auto limited = analyze(band_tones, 1024, 0);
    EXPECT_TRUE(limited.band_valid[2]);
    EXPECT_FALSE(limited.band_valid[3]);
    EXPECT_FALSE(analyze({}, 1024, 0).valid);
    EXPECT_FALSE(analyze(samples, 0, 0).valid);
    EXPECT_FALSE(analyze(samples, 1024, 3).valid);
    auto silence = samples;
    for (auto& s : silence) s = {1, 1, 1};
    EXPECT_TRUE(analyze(silence, 1024, 0).crest == 0);
    EXPECT_TRUE(analyze(silence, 1024, 0).frequency == 0);
    EXPECT_FALSE(analyze(samples, std::numeric_limits<float>::infinity(), 0).valid);
    EXPECT_FALSE(analyze(samples, std::numeric_limits<float>::quiet_NaN(), 0).valid);

    // Protect normalization, cached plan size changes, and the 2048 FFT cap.
    for (const std::size_t count : {8U, 32U, 256U, 1024U, 2048U, 3073U, 64U}) {
        const std::size_t n = count > 2048 ? 2048 : count;
        const std::size_t bin = n == 8 ? 2 : n / 8;
        std::vector<Axes> tone(count);
        for (std::size_t i = 0; i < count; ++i)
            tone[i] = {1.F + .001F * static_cast<float>(std::sin(2.0 * 3.141592653589793 * bin * i / n)), 0, 0};
        const auto ac = analyze(tone, static_cast<float>(n), 0);
        EXPECT_TRUE(ac.valid && ac.spectrum.size() == n / 2 + 1);
        EXPECT_TRUE(ac.bin_hz == 1.F && ac.frequency == static_cast<float>(bin));
        EXPECT_TRUE(std::abs(ac.spectrum[bin] - .001F) < (n == 8 ? .00005F : .000002F));
        EXPECT_TRUE(ac.spectrum[0] == 0);
    }
    // A mean between adjacent floats must not be rounded back onto 1 g before
    // subtraction: this signal has a 0.25-ULP mean and nonzero AC energy.
    std::vector<Axes> tiny(1024, Axes{1.F, 0, 0});
    for (std::size_t i = 0; i < tiny.size(); i += 4) tiny[i][0] = std::nextafter(1.F, 2.F);
    const auto micro = analyze(tiny, 1024, 0);
    EXPECT_TRUE(std::abs(micro.rms - 5.16191366e-8F) < 1e-11F);
    EXPECT_TRUE(std::abs(micro.kurtosis - 2.33333333F) < .001F);
    EXPECT_TRUE(std::abs(micro.peak - 8.94069672e-8F) < 1e-11F);

    std::vector<Axes> nyquist(256);
    for (std::size_t i = 0; i < nyquist.size(); ++i) nyquist[i] = {3.F + (i % 2 ? -.25F : .25F), 0, 0};
    const auto edge = analyze(nyquist, 2048, 0);
    EXPECT_TRUE(edge.frequency == 1024.F);
    EXPECT_TRUE(std::abs(edge.spectrum.back() - .25F) < .00001F);
    EXPECT_TRUE(std::abs(edge.rms - .25F) < .00001F);
    for (std::size_t i = 0; i < nyquist.size(); ++i)
        nyquist[i][0] = static_cast<float>(std::cos(2.0 * 3.141592653589793 * 127 * i / 256));
    EXPECT_TRUE(analyze(nyquist, 2048, 0).frequency == 1016.F);

    // An independent double-precision DFT checks every bin, not just the peak;
    // catches twiddle ordering, imaginary-sign, and size-dependent stale-cache bugs.
    for (const std::size_t n : {8U, 16U, 64U, 128U, 32U}) {
        std::vector<Axes> mixed(n);
        double mean = 0, gain = 0;
        for (std::size_t i = 0; i < n; ++i) {
            mixed[i] = {0, static_cast<float>(.3 * std::cos(.71 * i) + .12 * std::sin(1.93 * i) + .04 * i), 0};
            mean += mixed[i][1];
            gain += .5 - .5 * std::cos(2 * 3.141592653589793 * i / (n - 1));
        }
        mean /= n;
        const auto fft = analyze(mixed, 4096, 1);
        for (std::size_t bin = 1; bin <= n / 2; ++bin) {
            double re = 0, im = 0;
            for (std::size_t i = 0; i < n; ++i) {
                const double value = (mixed[i][1] - mean) * (.5 - .5 * std::cos(2 * 3.141592653589793 * i / (n - 1)));
                const double phase = -2 * 3.141592653589793 * bin * i / n;
                re += value * std::cos(phase);
                im += value * std::sin(phase);
            }
            const double amplitude = std::sqrt(re * re + im * im) * (bin == n / 2 ? 1 : 2) / gain;
            EXPECT_TRUE(std::abs(fft.spectrum[bin] - amplitude) < 2e-6);
        }
    }
    // Statistics use the complete sample window; the FFT removes the DC of
    // its own last-2048 window, not the mean of a different history interval.
    std::vector<Axes> offset_step(4096, Axes{5, 0, 0});
    for (std::size_t i = 2048; i < offset_step.size(); ++i) offset_step[i][0] = 1;
    const auto step = analyze(offset_step, 4096, 0);
    EXPECT_TRUE(step.rms == 2.F && step.peak_to_peak == 4.F);
    for (const float amplitude : step.spectrum) EXPECT_TRUE(amplitude == 0);
    const auto short_stats = analyze(offset_step, 4096, 0, 1024);
    EXPECT_TRUE(short_stats.sample_count == 1024);
    EXPECT_TRUE(short_stats.rms == 0 && short_stats.spectrum == step.spectrum);

    SampleHistory history;
    history.push({1, 2, 3}, 1, 4, 10);
    history.push({4, 5, 6}, 1, 4, 11);
    EXPECT_TRUE(history.copy(8).size() == 2);
    const auto generation = history.generation();
    history.push({7, 8, 9}, 1, 4, 13);
    EXPECT_TRUE(history.copy(8).size() == 1);  // a gap cannot enter one FFT window
    EXPECT_TRUE(history.generation() > generation);
    history.push({1, 2, 3}, 2, 4, 0);
    EXPECT_TRUE(history.copy(8).size() == 1);
    constexpr std::uint64_t written = SampleHistory::kCapacity + 808;
    for (std::uint64_t i = 1; i < written; ++i) history.push({static_cast<float>(i), 0, 0}, 2, 4, i);
    EXPECT_TRUE(history.size() == SampleHistory::kCapacity);
    EXPECT_TRUE(history.copy(2).front()[0] == static_cast<float>(written - 2));
    EXPECT_TRUE(history.copy(2).back()[0] == static_cast<float>(written - 1));

    vhm::probe::CalibrationRecord cal;
    cal.valid_flags          = 7;
    cal.adxl_accel_bias_g[0] = .1F;
    cal.adxl_accel_scale     = {2, 1, 1};
    cal.icm_accel_scale      = {1, 1, 1};
    cal.icm_gyro_bias_dps[0] = 1;
    EXPECT_TRUE(std::abs(convert_accel(52429, 0, true, cal) - 1.8F) < .001F);
    EXPECT_TRUE(std::abs(convert_accel(2048, 0, false, cal) - 1.F) < .001F);
    EXPECT_TRUE(std::abs(convert_gyro(16384, 0, cal) - 249.F) < .001F);
    cal.valid_flags = 0;
    EXPECT_TRUE(std::abs(convert_accel(52429, 0, true, cal) - 1.F) < .001F);
    EXPECT_TRUE(std::abs(convert_accel(52429, 0, true, cal, 40) - 4.F) < .001F);
    EXPECT_TRUE(std::abs(convert_accel(4096, 0, false, cal, 8) - 1.F) < .001F);

    // A blocked FFT must never block the UI's publish/read path. Pending work
    // must collapse to the latest frame instead of creating a growing backlog.
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    std::atomic<unsigned> calls{0};
    ScopeAnalysisWorker worker([&](const std::vector<Axes>& data, float, std::size_t, std::size_t, std::size_t) {
        if (calls.fetch_add(1) == 0) {
            entered.set_value();
            released.wait();
        }
        Analysis output;
        output.valid = true;
        output.rms   = data.front()[0];
        return output;
    });
    worker.start();
    auto input = [](unsigned serial) {
        ScopeAnalysisInput frame;
        for (unsigned source = 0; source < 2; ++source) {
            frame.valid[source]      = true;
            frame.near_full_scale[source] = serial == 3;
            frame.generation[source] = 7;
            frame.serial[source]     = serial;
            frame.rate[source]       = 4000;
            frame.spectrum_bins[source] = serial == 3 ? 8192U : 2048U;
            frame.samples[source].assign(8, Axes{static_cast<float>(serial), 0, 0});
        }
        return frame;
    };
    worker.submit(input(1));
    entered.get_future().wait();
    auto ui = std::async(std::launch::async, [&] {
        worker.submit(input(2));
        worker.submit(input(3));
        return !worker.latest();
    });
    EXPECT_TRUE(ui.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    release.set_value();
    EXPECT_TRUE(ui.get());
    std::shared_ptr<const ScopeAnalysisOutput> output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    do {
        output = worker.latest();
        if (output && output->serial[0] == 3) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    worker.stop();
    EXPECT_TRUE(output && output->serial[0] == 3);
    EXPECT_TRUE(calls == 12);
    if (output) {
        EXPECT_TRUE(output->axes[0][0].rms == 3.F);
        EXPECT_TRUE(output->spectrum_bins[0] == 8192U);
        EXPECT_TRUE(output->near_full_scale[0] && output->near_full_scale[1]);
        EXPECT_TRUE(output->usable(0, true, 7, 4, 4000));
        EXPECT_TRUE(output->usable(0, true, 7, 4, 4001));  // estimator drift is not a new ODR
        EXPECT_FALSE(output->usable(0, true, 8, 4, 4000));
        EXPECT_FALSE(output->usable(0, false, 7, 4, 4000));
        EXPECT_FALSE(output->usable(0, true, 7, 4, 8000));
        EXPECT_FALSE(output->usable(0, true, 7, 2, 4000));
    }

    // Rendering can be re-entered after a mode change: Gyro's third sample
    // stream must never become an index into the two-source FFT results.
    PlotConfig config;
    config.kind = PlotKind::Gyro;
    config.normalize_source();
    EXPECT_TRUE(config.source == Source::Gyro);
    for (auto kind : {PlotKind::Time, PlotKind::Spectrum, PlotKind::Waterfall}) {
        config.source = Source::Gyro;
        config.kind = kind;
        config.normalize_source();
        EXPECT_TRUE(config.source == Source::Icm);
    }
    config.source = Source::Adxl;
    config.normalize_source();
    EXPECT_TRUE(config.source == Source::Adxl);

    // Raster edges, missing-data gaps, and odd-width stride padding must never
    // touch guard pixels. This uses no LVGL or device-specific implementation.
    constexpr std::int32_t missing = INT32_MAX;
    ScopePlotPoints points;
    for (auto& axis : points) axis.fill(missing);
    points[0][0]   = 2000;
    points[0][255] = -2000;
    std::array<std::uint16_t, 8 * 5 + 2> guarded;
    guarded.fill(0xabcd);
    raster_scope_plot(guarded.data() + 1, 7, 5, 8, points, false, missing, 0, 1, {2, 3, 4});
    EXPECT_TRUE(guarded.front() == 0xabcd && guarded.back() == 0xabcd);
    EXPECT_TRUE(guarded[1] == 2);              // clipped top-left
    EXPECT_TRUE(guarded[1 + 4 * 8 + 6] == 2);  // clipped bottom-right
    EXPECT_TRUE(guarded[1 + 2 * 8 + 3] != 2);  // no line across the missing interval
    for (unsigned row = 0; row < 5; ++row) EXPECT_TRUE(guarded[1 + row * 8 + 7] == 0);
    for (auto& axis : points) axis.fill(missing);
    points[1].fill(500);
    raster_scope_plot(guarded.data() + 1, 7, 5, 8, points, true, missing, 0, 1, {2, 3, 4});
    EXPECT_TRUE(guarded[1 + 2 * 8 + 3] == 3);  // positive-only FFT half-scale
    EXPECT_TRUE(guarded.front() == 0xabcd && guarded.back() == 0xabcd);
}
