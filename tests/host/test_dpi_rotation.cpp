/**
 * @file test_dpi_rotation.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-04
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "test_support.hpp"
#include "../../platforms/tab5/components/espressif__esp_lvgl_port/src/lvgl9/lvgl_port_dpi_rect.h"

void run_dpi_rotation_tests()
{
    lvgl_port_dpi_rect_t r{};
    EXPECT_TRUE(lvgl_port_dpi_rotate90_rect(720, 1280, 0, 0, 1279, 719, &r));
    EXPECT_TRUE(r.x == 0 && r.y == 0 && r.width == 720 && r.height == 1280);
    EXPECT_TRUE(lvgl_port_dpi_rotate90_rect(720, 1280, 53, 65, 956, 264, &r));
    EXPECT_TRUE(r.x == 65 && r.y == 323 && r.width == 200 && r.height == 904);
    EXPECT_TRUE(lvgl_port_dpi_rotate90_rect(720, 1280, 1279, 719, 1279, 719, &r));
    EXPECT_TRUE(r.x == 719 && r.y == 0 && r.width == 1 && r.height == 1);
    EXPECT_TRUE(lvgl_port_dpi_rotate90_rect(720, 1280, 0, 0, 0, 0, &r));
    EXPECT_TRUE(r.x == 0 && r.y == 1279 && r.width == 1 && r.height == 1);
    EXPECT_TRUE(!lvgl_port_dpi_rotate90_rect(720, 1280, -1, 0, 10, 10, &r));
    EXPECT_TRUE(!lvgl_port_dpi_rotate90_rect(720, 1280, 0, 0, 1280, 10, &r));
    EXPECT_TRUE(!lvgl_port_dpi_rotate90_rect(720, 1280, 0, 0, 10, 720, &r));
    EXPECT_TRUE(!lvgl_port_dpi_rotate90_rect(720, 1280, 10, 0, 9, 10, &r));
    EXPECT_TRUE(!lvgl_port_dpi_rotate90_rect(0, 1280, 0, 0, 10, 10, &r));
    EXPECT_TRUE(!lvgl_port_dpi_rotate90_rect(720, 1280, 0, 0, 10, 10, nullptr));
}
