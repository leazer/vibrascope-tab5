/**
 * @file demo_data.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "demo_data.h"

#include <cmath>

namespace vibescope::demo {
namespace {
double pseudo_random(int i)
{
    const double v = std::sin(i * 12.9898) * 43758.5453;
    return v - std::floor(v);
}

double gaussian(double x, double center, double sigma)
{
    const double d = (x - center) / sigma;
    return std::exp(-d * d);
}
}  // namespace

void fill_wave(lv_point_precise_t* points, std::uint32_t count, std::int32_t w, std::int32_t h)
{
    const double mid = h * 0.5;
    for (std::uint32_t i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / (count - 1);
        const bool impact = t > 0.42 && t < 0.465;
        double amp = 0.055 + 0.02 * std::abs(std::sin(t * 41.0)) +
                     0.014 * std::abs(std::sin(t * 113.0 + 1.7)) + 0.012 * pseudo_random(i);
        amp *= impact ? 8.5 : 1.0;
        points[i] = {static_cast<lv_value_precise_t>(t * (w - 4) + 2),
                     static_cast<lv_value_precise_t>(mid - amp * h * 0.42 * 1000.0 / 1000.0)};
    }
}

void fill_spectrum(lv_point_precise_t* points, std::uint32_t count, std::int32_t w, std::int32_t h)
{
    for (std::uint32_t i = 0; i < count; ++i) {
        const double f = static_cast<double>(i) / (count - 1) * 2000.0;
        double v = 0.02 + 0.012 * pseudo_random(i) + 0.03 * std::exp(-f / 220.0);
        v += 0.118 * gaussian(f, 49.8, 2.2) + 0.042 * gaussian(f, 99.6, 2.5) +
             0.020 * gaussian(f, 149.4, 3.0) + 0.009 * gaussian(f, 199.2, 3.5);
        points[i] = {static_cast<lv_value_precise_t>(static_cast<double>(i) / (count - 1) * (w - 4) + 2),
                     static_cast<lv_value_precise_t>(h - 4 - v * (h - 20) * 6.0)};
    }
}

void fill_trend(lv_point_precise_t* points, std::uint32_t count, std::int32_t w, std::int32_t h)
{
    for (std::uint32_t i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / (count - 1);
        const double v = 0.38 + 0.012 * std::sin(t * 9.0) + 0.02 * pseudo_random(i) + (t > 0.45 ? 0.05 : 0.0);
        points[i] = {static_cast<lv_value_precise_t>(t * (w - 4) + 2),
                     static_cast<lv_value_precise_t>(h - 6 - v * (h - 20))};
    }
}

void fill_gyro(lv_point_precise_t* points, std::uint32_t count, std::int32_t w, std::int32_t h, int axis)
{
    static constexpr double kPhase[3] = {0.0, 2.1, 4.2};
    const double amp = 34.0 - axis * 8.0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / (count - 1);
        points[i] = {static_cast<lv_value_precise_t>(t * (w - 4) + 2),
                     static_cast<lv_value_precise_t>(h * 0.5 -
                                                     std::sin(t * 22.0 + kPhase[axis]) * amp * h / 120.0 -
                                                     0.008 * pseudo_random(i + axis * 7) * h)};
    }
}

}  // namespace vibescope::demo
