/**
 * @file style.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <lvgl.h>

extern "C" {
LV_FONT_DECLARE(vn_font_16);
LV_FONT_DECLARE(vn_font_18);
LV_FONT_DECLARE(vn_font_32);
LV_FONT_DECLARE(vn_status_icons_24);
LV_IMAGE_DECLARE(vn_logo);
}

namespace vibescope_view {
constexpr int kWidth = 1280, kHeight = 720, kSide = 260;
// Neutral surfaces keep the waveform area dark; yellow marks actions and focus.
constexpr unsigned kBackground = 0x181818, kPanel = 0x222222, kPlot = 0x121212, kLine = 0x383838;
constexpr unsigned kText = 0xf2f2ed, kMuted = 0xaaaaaa, kAccent = 0xffc400, kSoft = 0x2d2d2d, kRed = 0xf28a85;
constexpr unsigned kOnAccent = 0x222222, kSelected = 0x4a401b, kGrid = 0x292929;
constexpr unsigned kAxes[3] = {0x5cdbce, 0xe5af5b, 0x8bafff};
inline void surface(lv_obj_t* object, unsigned color = kPanel, int radius = 0)
{
    lv_obj_remove_style_all(object);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}
inline void position(lv_obj_t* object, int x, int y, int w, int h)
{
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, w, h);
}
}  // namespace vibescope_view
