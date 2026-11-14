/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_startup_anim.h"
#include <app.h>
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <cstdio>
#include <chrono>
#include <algorithm>
#include "../app_vibescope/scope_ppa.h"
#include "../app_vibescope/view/style.h"

extern "C" const lv_image_dsc_t vn_startup_logo;
extern "C" const lv_image_dsc_t vn_startup_dot;
extern "C" const int32_t vn_startup_dot_x, vn_startup_dot_y;

using namespace mooncake;
using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

AppStartupAnim::AppStartupAnim()
{
    setAppInfo().name = "AppStartupAnim";
}

static uint64_t render_time_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

void AppStartupAnim::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppStartupAnim::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    LvglLockGuard lock;
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(vibescope_view::kPanel), LV_PART_MAIN);
    lv_obj_remove_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);
    vibescope::init_ppa_draw();

    // Keep one bounded composition, rather than repainting the whole display.
    const auto* display = lv_display_get_default();
    const int width = lv_display_get_horizontal_resolution(display);
    const int height = lv_display_get_vertical_resolution(display);
    _backdrop = std::make_unique<Container>(lv_screen_active());
    lv_obj_remove_style_all(_backdrop->get());
    _backdrop->setBgColor(lv_color_hex(vibescope_view::kPanel));
    _backdrop->setBgOpa(LV_OPA_COVER);
    _backdrop->setPos((width - 592) / 2, height / 2 - 108);
    _backdrop->setSize(592, 268);
    _backdrop->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _backdrop->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    // Center the complete lockup/tagline/version group independently of resolution.
    _logo = std::make_unique<Image>(lv_screen_active());
    _logo->setSrc(&vn_startup_logo);
    _logo->align(LV_ALIGN_CENTER, 0, -16);
    _logo->setOpa(0);

    _dot = std::make_unique<Image>(lv_screen_active());
    _dot->setSrc(&vn_startup_dot);
    _dot->setOpa(0);
    _dot_frame_pending = _dot_frame_ready = false;
    lv_display_add_event_cb(lv_display_get_default(), display_event, LV_EVENT_ALL, this);

    _label_tagline = std::make_unique<Label>(lv_screen_active());
    _label_tagline->setText("VIBRATION ANALYSIS");
    _label_tagline->setTextFont(&lv_font_montserrat_24);
    _label_tagline->setTextColor(lv_color_hex(vibescope_view::kMuted));
    _label_tagline->align(LV_ALIGN_CENTER, 0, 82);
    _label_tagline->setOpa(0);

    _label_version = std::make_unique<Label>(lv_screen_active());
    _label_version->setText(app::kFirmwareVersion);
    _label_version->setTextFont(&lv_font_montserrat_16);
    _label_version->setTextColor(lv_color_hex(vibescope_view::kMuted));
    _label_version->align(LV_ALIGN_CENTER, 0, 122);
    _label_version->setOpa(0);

    _anim_offset_y.pause();
    _anim_offset_y.teleport(32);
    _anim_offset_y.springOptions().visualDuration = 0.6;
    _anim_offset_y.springOptions().bounce         = 0.05;
    _anim_opacity.pause();
    _anim_opacity.teleport(0);
    _anim_opacity.easingOptions().easingFunction = ease::linear;
    _anim_opacity.easingOptions().duration       = 0.6;
    // Select the generator before teleporting: switching spring -> easing
    // replaces its stored start/end values in smooth_ui_toolkit.
    _anim_dot_offset_y.easingOptions().duration = 0.48;
    _anim_dot_offset_y.easingOptions().easingFunction = ease::ease_in_quad;
    const int dot_bottom =
        (lv_display_get_vertical_resolution(lv_display_get_default()) - static_cast<int>(vn_startup_logo.header.h)) / 2 -
        48 + vn_startup_dot_y + vn_startup_dot.header.h;
    _anim_dot_offset_y.teleport(-dot_bottom);
    _anim_dot_offset_y.pause();
    _anime_state                                 = AnimState_StartupDelay;
    _is_sfx_played                               = false;
    _time_count                                  = GetHAL()->millis();
    _frame_tick = _time_count;
    _reveal_frames = _drop_frames = 0;
    _render_started = _render_sum = _render_max = 0;
}

void AppStartupAnim::onRunning()
{
    LvglLockGuard lock;
    if (_anime_state == AnimState_StartupDelay) {
        if (GetHAL()->millis() - _time_count <= 400) return;
        _anime_state = AnimState_LogoReveal;
        _time_count = GetHAL()->millis();
        _frame_tick = _time_count - 16;
        _anim_offset_y.play();
        _anim_offset_y = 0;
        _anim_opacity.play();
        _anim_opacity = 255;
        mclog::tagInfo(getAppInfo().name, "logo reveal at {} ms", GetHAL()->millis());
    }
    if (_anime_state == AnimState_LogoReveal) {
        if ((_anim_offset_y.done() && _anim_opacity.done()) || GetHAL()->millis() - _time_count >= 1000) {
            _anime_state = AnimState_DotDrop;
            _logo->align(LV_ALIGN_CENTER, 0, -48);
            lv_obj_update_layout(_logo->get());
            _dot_target_x = lv_obj_get_x(_logo->get()) + vn_startup_dot_x;
            _dot_target_y = lv_obj_get_y(_logo->get()) + vn_startup_dot_y;
            _anim_dot_offset_y.teleport(-(_dot_target_y + static_cast<int>(vn_startup_dot.header.h)));
            _anim_dot_offset_y.play();
            _anim_dot_offset_y = 0;
            _dot->setOpa(255);
            _backdrop->setPos(_backdrop->getX(), 0);
            _backdrop->setSize(592, lv_display_get_vertical_resolution(lv_display_get_default()) / 2 + 160);
            _frame_tick = GetHAL()->millis() - 16;
            mclog::tagInfo(getAppInfo().name, "dot drop at {} ms", GetHAL()->millis());
            // C6 WiFi remains disabled: the hosted handshake fails on this board.
            // See docs/milestones/2026-09-10-lvgl-clib-malloc-fix.md.
        }
    } else if (_anime_state == AnimState_AwaitDing && _dot_frame_ready && !_is_sfx_played) {
        // The landing frame has been submitted to the display, rather than
        // merely changing coordinates ahead of the next LVGL refresh.
        GetHAL()->playStartupSfx();
        _is_sfx_played = true;
        _anime_state = AnimState_FinalDelay;
        _time_count = GetHAL()->millis();
        mclog::tagInfo(getAppInfo().name, "dot landed / startup chime");
        return;
    } else if (_anime_state == AnimState_FinalDelay) {
        if (GetHAL()->millis() - _time_count > 1000) close();
        return;
    }

    if (_anime_state == AnimState_AwaitDing) return;
    const auto now = GetHAL()->millis();
    if (now - _frame_tick < 16) return;
    _frame_tick = now;
    // The children's dirty rectangles are contained by this one composition.
    lv_obj_invalidate(_backdrop->get());

    const bool revealing = _anime_state == AnimState_LogoReveal;
    const int offset   = revealing ? static_cast<int>(_anim_offset_y.value()) : 0;
    const auto opacity = revealing ? static_cast<lv_opa_t>(_anim_opacity.value()) : static_cast<lv_opa_t>(LV_OPA_COVER);
    _logo->align(LV_ALIGN_CENTER, 0, -48 + offset);
    _label_tagline->align(LV_ALIGN_CENTER, 0, 50 + offset);
    _label_version->align(LV_ALIGN_CENTER, 0, 90 + offset);
    _logo->setOpa(opacity);
    _label_tagline->setOpa(opacity);
    _label_version->setOpa(opacity);
    const int dot_offset = _anim_dot_offset_y;
    _dot->setPos(_dot_target_x, _dot_target_y + dot_offset);
    if (_anime_state == AnimState_DotDrop && _anim_dot_offset_y.done()) {
        _anime_state = AnimState_AwaitDing;
        _dot_frame_pending = true;
        mclog::tagInfo(getAppInfo().name, "dot landing frame at {} ms", GetHAL()->millis());
    }
}

void AppStartupAnim::display_event(lv_event_t* event)
{
    auto* self = static_cast<AppStartupAnim*>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_RENDER_START) {
        self->_render_started = render_time_us();
        self->_render_phase = self->_anime_state == AnimState_LogoReveal ? 1 :
                              (self->_anime_state == AnimState_DotDrop || self->_anime_state == AnimState_AwaitDing) ? 2 : 0;
    } else if (code == LV_EVENT_RENDER_READY && self->_render_started && self->_render_phase) {
        const auto elapsed = render_time_us() - self->_render_started;
        self->_render_sum += elapsed;
        self->_render_max = std::max(self->_render_max, elapsed);
        if (self->_render_phase == 1) ++self->_reveal_frames;
        else ++self->_drop_frames;
        self->_render_started = 0;
    }
    if (code == LV_EVENT_FLUSH_FINISH && self->_dot_frame_pending) {
        self->_dot_frame_pending = false;
        self->_dot_frame_ready = true;
    }
}

void AppStartupAnim::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    LvglLockGuard lock;
    lv_display_remove_event_cb_with_user_data(lv_display_get_default(), display_event, this);
    const auto frames = _reveal_frames + _drop_frames;
    std::printf("startup-perf: reveal_frames=%u drop_frames=%u avg_us=%llu max_us=%llu\n",
                static_cast<unsigned>(_reveal_frames), static_cast<unsigned>(_drop_frames),
                static_cast<unsigned long long>(frames ? _render_sum / frames : 0),
                static_cast<unsigned long long>(_render_max));
    _dot.reset();
    _logo.reset();
    _label_tagline.reset();
    _label_version.reset();
    _backdrop.reset();
}
