/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <memory>
#include "lvgl_cpp/label.h"
#include <smooth_ui_toolkit.h>
#include <smooth_lvgl.h>
#include <stdint.h>

/**
 * @brief 派生 App
 *
 */
class AppStartupAnim : public mooncake::AppAbility {
public:
    AppStartupAnim();

    // 重写生命周期回调
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum AnimState_t {
        AnimState_StartupDelay = 0, AnimState_LogoReveal, AnimState_DotDrop, AnimState_AwaitDing, AnimState_FinalDelay
    };

    AnimState_t _anime_state = AnimState_StartupDelay;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _backdrop;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> _logo;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> _dot;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _label_tagline;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _label_version;
    smooth_ui_toolkit::AnimateValue _anim_offset_y;
    smooth_ui_toolkit::AnimateValue _anim_opacity;
    smooth_ui_toolkit::AnimateValue _anim_dot_offset_y;
    bool _dot_frame_pending = false, _dot_frame_ready = false;
    int32_t _dot_target_x = 0, _dot_target_y = 0;
    static void display_event(lv_event_t* event);
    uint32_t _time_count = 0;
    bool _is_sfx_played  = false;
    uint32_t _frame_tick = 0, _reveal_frames = 0, _drop_frames = 0;
    uint64_t _render_started = 0, _render_sum = 0, _render_max = 0;
    unsigned _render_phase = 0;
};
