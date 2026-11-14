/**
 * @file demo_data.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <lvgl.h>
#include <cstdint>

namespace vibescope::demo {

/* M1 静态占位曲线,确定性生成;M2/M3 换真数据源时保持签名不变 */
void fill_wave(lv_point_precise_t* points, std::uint32_t count, std::int32_t w, std::int32_t h);
void fill_spectrum(lv_point_precise_t* points, std::uint32_t count, std::int32_t w, std::int32_t h);
void fill_trend(lv_point_precise_t* points, std::uint32_t count, std::int32_t w, std::int32_t h);
void fill_gyro(lv_point_precise_t* points, std::uint32_t count, std::int32_t w, std::int32_t h, int axis);

}  // namespace vibescope::demo
