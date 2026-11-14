/**
 * @file scope_calibration.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-10
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include "scope_analysis.h"
#include <cmath>

namespace vibescope {

enum class CalibrationStage {
    Idle, Preparing, Place, Capture, Review, Saving, Restoring, Saved, Cancelled, Failed
};

struct CalibrationStep {
    unsigned face{}, sensor{}; // ADXL first, then ICM on the same face.

    bool accept()
    {
        if (sensor == 0) {
            sensor = 1;
            return false;
        }
        if (face == 5) return true;
        ++face;
        sensor = 0;
        return false;
    }
};

struct CalibrationView {
    CalibrationStage stage{CalibrationStage::Idle};
    CalibrationStep step;
    std::array<Axes, 2> accel{};
    Axes gyro{};
    std::array<bool, 2> accel_live{};
    bool gyro_live{}, aligned{}, still{}, retry{}, result_ready{};
    vhm::probe::CalibrationRecord result{};
    unsigned remaining_ms{};
    const char* error{};
};

inline bool calibration_aligned(unsigned face, const Axes& accel)
{
    if (face >= 6) return false;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const float expected = axis == face / 2 ? (face % 2 ? -1.F : 1.F) : 0.F;
        if (!(std::abs(accel[axis] - expected) <= .15F)) return false;
    }
    return true;
}

inline bool calibration_gyro_still(const Axes& gyro)
{
    for (float rate : gyro)
        if (!(std::abs(rate) <= 3.F)) return false;
    return true;
}

class CalibrationPlacement {
public:
    static constexpr unsigned hold_ms = 750;
    static constexpr unsigned freshness_ms = 150;

    void reset() { _holding = false; }

    unsigned update(unsigned face, const std::array<Axes, 2>& accel, const Axes& gyro, std::uint64_t now)
    {
        if (!calibration_aligned(face, accel[0]) || !calibration_aligned(face, accel[1]) ||
            !calibration_gyro_still(gyro)) {
            reset();
            return hold_ms;
        }
        if (_holding) {
            if (_face != face || now - _last_ms > freshness_ms) reset();
            for (unsigned sensor = 0; sensor < accel.size(); ++sensor)
                for (unsigned axis = 0; axis < 3; ++axis)
                    if (std::abs(accel[sensor][axis] - _reference[sensor][axis]) > .035F) reset();
        }
        if (!_holding) {
            _holding = true;
            _face = face;
            _start_ms = now;
            _reference = accel;
        }
        _last_ms = now;
        const auto elapsed = now - _start_ms;
        return elapsed >= hold_ms ? 0U : hold_ms - static_cast<unsigned>(elapsed);
    }

private:
    bool _holding{};
    unsigned _face{};
    std::uint64_t _start_ms{}, _last_ms{};
    std::array<Axes, 2> _reference{};
};

}  // namespace vibescope
