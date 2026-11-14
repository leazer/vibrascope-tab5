/**
 * @file pose_geometry.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-11
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <cmath>
#include <lvgl.h>

namespace vibescope_view {
struct PoseProjection {
    float cx, cy, scale;
    float cp, sp, cr, sr, cyaw, syaw;

    PoseProjection(float x, float y, float size, float pitch, float roll, float yaw)
        : cx(x), cy(y), scale(size), cp(std::cos(pitch)), sp(std::sin(pitch)),
          cr(std::cos(roll)), sr(std::sin(roll)), cyaw(std::cos(yaw)), syaw(std::sin(yaw)) {}

    lv_point_t operator()(float x, float y, float z) const
    {
        const float yr = y * cr - z * sr, zr = y * sr + z * cr;
        const float xp = x * cp + zr * sp, zp = -x * sp + zr * cp;
        const float xy = xp * cyaw - yr * syaw, yy = xp * syaw + yr * cyaw;
        // In the sensor frame Z is up and +Y points into the screen.
        const float camera_x = xy * .866F + yy * .5F, camera_z = xy * .5F - yy * .866F;
        return {static_cast<int32_t>(cx + camera_x * scale),
                static_cast<int32_t>(cy - (zp * .866F - camera_z * .5F) * scale)};
    }

    lv_point_t axis(unsigned index, float length) const
    {
        return (*this)(index == 0 ? length : 0, index == 1 ? length : 0, index == 2 ? length : 0);
    }
};

inline constexpr float kPoseVertices[8][3] = {
    {-1, -1, -.5F}, {1, -1, -.5F}, {1, 1, -.5F}, {-1, 1, -.5F},
    {-1, -1, .5F}, {1, -1, .5F}, {1, 1, .5F}, {-1, 1, .5F}
};
inline constexpr unsigned kPoseEdges[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7}
};
}  // namespace vibescope_view
