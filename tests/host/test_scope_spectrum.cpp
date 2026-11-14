/**
 * @file test_scope_spectrum.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-04
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "test_support.hpp"
#include "apps/app_vibescope/scope_viewport.h"
#include "apps/app_vibescope/scope_analysis.h"
#include <vector>
#include <cmath>

void run_scope_spectrum_tests()
{
    using namespace vibescope;
    std::vector<float> bins{0, 0, 0, 0, 10, 0, 0, 0};
    std::vector<std::size_t> indices;
    auto value = [&](std::size_t i) { return bins[i]; };
    auto emit = [&](std::size_t i, float) { indices.push_back(i); };
    visit_spectrum(0, bins.size(), 2, value, emit);
    unsigned peaks = 0;
    for (std::size_t i = 0; i < indices.size(); ++i) {
        peaks += indices[i] == 4;
        if (i) EXPECT_TRUE(indices[i] > indices[i - 1]);
    }
    EXPECT_TRUE(peaks == 1);
    EXPECT_TRUE(indices.front() == 0 && indices.back() == 7);
    indices.clear();
    visit_spectrum(2, 4, 20, value, emit);
    EXPECT_TRUE(indices.size() == 4 && indices.front() == 2 && indices.back() == 5);
    indices.clear();
    visit_spectrum(0, 0, 0, value, emit);
    EXPECT_TRUE(indices.empty());
    EXPECT_TRUE(std::abs(spectrum_db(1.F)) < .0001F);
    EXPECT_TRUE(std::abs(spectrum_db(.01F) + 40.F) < .0001F);
    EXPECT_TRUE(std::isfinite(spectrum_db(0.F)) && spectrum_db(0.F) == -160.F);

    SampleViewport view;
    SpectrumGrid grid;
    grid.sync(view, 513, 4000.F / 1024);
    EXPECT_TRUE(view.first == 0 && view.count == 513);
    view.zoom(513, .5F);
    const auto zoomed = view.count;
    grid.sync(view, 513, 4000.F / 1024);
    EXPECT_TRUE(view.count == zoomed);
    grid.sync(view, 1025, 4000.F / 2048);
    EXPECT_TRUE(view.first == 0 && view.count == 1025);
    view.zoom(1025, .5F);
    grid.sync(view, 1025, 8000.F / 2048);
    EXPECT_TRUE(view.first == 0 && view.count == 1025);

    Analysis x, y;
    x.spectrum.resize(1025);
    std::uint32_t noise = 1;
    for (auto& bin : x.spectrum) {
        noise = 1664525U * noise + 1013904223U;
        bin = .001F * static_cast<float>(std::sqrt(-std::log((double(noise) + 1.0) / 4294967297.0)));
    }
    const std::array<const Analysis*, 3> visible{&x, &y, nullptr};
    EXPECT_TRUE(spectrum_peaks(visible, 0, 1025, 3)[0].bin == 0);
    y.spectrum = x.spectrum;
    x.spectrum[20] = .10F;
    y.spectrum[21] = .20F; // One physical peak shown by two axes.
    x.spectrum[65] = .05F;
    x.spectrum[150] = .012F;
    x.spectrum[160] = .003F; // A local maximum close to the noise floor.
    const auto selected = spectrum_peaks(visible, 0, 1025, 3);
    EXPECT_TRUE(selected[0].bin == 21 && selected[0].axis == 1);
    EXPECT_TRUE(selected[1].bin == 65 && selected[2].bin == 150);
    EXPECT_TRUE(spectrum_peaks(visible, 22, 100, 3)[0].bin == 65);
    EXPECT_TRUE(spectrum_peaks(visible, 0, 1025, 0)[0].bin == 0);
}
