/**
 * @file test_scope_calibration.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-10
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "test_support.hpp"
#include <apps/app_vibescope/scope_calibration.h>

void run_scope_calibration_tests()
{
    using namespace vibescope;
    CalibrationPlacement placement;
    for (unsigned face = 0; face < 6; ++face) {
        Axes accel{};
        accel[face / 2] = face % 2 ? -1.0F : 1.0F;
        EXPECT_TRUE(calibration_aligned(face, accel));
        EXPECT_FALSE(calibration_aligned(face ^ 1U, accel));
    }
    const Axes up{0, 0, 1};
    const Axes still{};
    const std::array<Axes, 2> both_up{up, up};
    EXPECT_FALSE(calibration_aligned(4, {0.2F, 0, 1}));
    EXPECT_TRUE(placement.update(4, both_up, still, 100) == 750);
    EXPECT_TRUE(placement.update(4, both_up, still, 200) == 650);
    EXPECT_TRUE(placement.update(4, both_up, {0, 4, 0}, 250) == 750);
    for (unsigned time = 300; time < 1050; time += 50)
        EXPECT_TRUE(placement.update(4, both_up, still, time) == 1050 - time);
    EXPECT_TRUE(placement.update(4, both_up, still, 1050) == 0);
    EXPECT_TRUE(placement.update(4, both_up, still, 1300) == 750);
    EXPECT_TRUE(placement.update(4, {up, Axes{0.05F, 0, 1}}, still, 1350) == 750);
    EXPECT_TRUE(placement.update(4, {up, Axes{0, 0, -1}}, still, 1400) == 750);
    EXPECT_TRUE(placement.update(4, both_up, still, 1450) == 750);

    CalibrationStep step;
    for (unsigned face = 0; face < 6; ++face) {
        EXPECT_TRUE(step.face == face && step.sensor == 0);
        EXPECT_FALSE(step.accept());
        EXPECT_TRUE(step.face == face && step.sensor == 1);
        EXPECT_TRUE(step.accept() == (face == 5));
    }
}
