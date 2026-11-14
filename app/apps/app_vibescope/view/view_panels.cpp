/**
 * @file view_panels.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "view_private.h"
#include <cstring>
#if !defined(PLATFORM_BUILD_DESKTOP)
#include <esp_lvgl_port.h>
#endif

namespace vibescope_view {
namespace {
bool released_inside(lv_event_t* event)
{
    auto* input = lv_event_get_indev(event);
    if (!input || lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER) return true;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_current_target_obj(event), &bounds);
    return point.x >= bounds.x1 && point.x <= bounds.x2 && point.y >= bounds.y1 && point.y <= bounds.y2;
}
}

void VibescopeView::Impl::build_sidebar()
{
    auto* side = _sidebar = panel(_screen->get(), 0, 0, kSide, 654);
    border(side);
    auto* brand = panel(side, 0, 0, kSide - 1, 60, kPanel);
    auto* logo  = lv_image_create(brand);
    lv_image_set_src(logo, &vn_logo);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 9);
    lv_obj_add_flag(brand, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        brand, [](lv_event_t* e) { static_cast<Impl*>(lv_event_get_user_data(e))->show_about(); }, LV_EVENT_CLICKED,
        this);

    auto* pose   = panel(side, 1, 60, kSide - 2, 100);
    _cube_canvas = lv_canvas_create(pose);
    lv_obj_set_pos(_cube_canvas, 5, 0);
    lv_canvas_set_buffer(_cube_canvas, _cube_pixels.data(), 112, 100, LV_COLOR_FORMAT_RGB565);
    for (unsigned axis = 0; axis < 3; ++axis)
        _cube_axis_labels[axis] = text(pose, axis_names[axis], 0, 0, kAxes[axis], &lv_font_montserrat_12);
    _probe_status = text(pose, "未连接", 0, 81, 0x686868, &vn_font_16);
    lv_obj_set_width(_probe_status, 120);
    lv_obj_set_style_text_align(_probe_status, LV_TEXT_ALIGN_CENTER, 0);
    panel(pose, 124, 8, 1, 82, kLine);
    for (unsigned i = 0; i < 3; ++i) {
        text(pose, i == 0 ? "Pitch" : i == 1 ? "Roll" : "Yaw", 138, 12 + i * 28, kMuted);
        _angle_labels[i] = text(pose, "--", 201, 12 + i * 28, kText, &lv_font_montserrat_16);
    }
    auto* pose_hit = _pose_hit = panel(pose, 0, 0, 120, 100, 0xffffff);
    lv_obj_set_style_bg_opa(pose_hit, LV_OPA_TRANSP, 0);
    _pose_flash.easingOptions().duration = .24;
    _pose_flash.easingOptions().easingFunction = ease::ease_out_quad;
    _pose_flash.teleport(0);
    lv_obj_add_flag(pose_hit, LV_OBJ_FLAG_CLICKABLE);
    bind_pose_reset(pose_hit);

    auto* metrics = panel(side, 1, 162, kSide - 2, 492);
    _metric_window = text(metrics, "ADXL357 | ±10g | 1.00s | --Hz", 4, 8, kMuted, &lv_font_montserrat_14);
    lv_obj_set_width(_metric_window, kSide - 10);
    lv_obj_set_style_text_align(_metric_window, LV_TEXT_ALIGN_CENTER, 0);
    auto* settings_hit = panel(metrics, 1, 0, kSide - 4, 40);
    lv_obj_set_style_bg_opa(settings_hit, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(settings_hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(settings_hit, [](lv_event_t* e) {
        static_cast<Impl*>(lv_event_get_user_data(e))->show_measurement_settings();
    }, LV_EVENT_CLICKED, this);
    auto* xyz = _xyz_metrics = panel(metrics, 0, 0, kSide - 2, 492);
    auto* vector = _resultant_metrics = panel(metrics, 0, 0, kSide - 2, 492);
    for (auto* group : {xyz, vector}) {
        lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(group, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_add_flag(vector, LV_OBJ_FLAG_HIDDEN);
    metrics = xyz;
    text(metrics, "振动指标 g", 12, 42, kMuted);
    for (unsigned axis = 0; axis < 3; ++axis) {
        auto* name = text(metrics, axis_names[axis], 91 + 52 * axis, 42, kAxes[axis]);
        lv_obj_set_width(name, 50);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_RIGHT, 0);
    }
    auto numbers = [&](std::array<lv_obj_t*, 3>& row, int y, const lv_font_t* font) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            row[axis] = text(metrics, "--", 91 + 52 * axis, y, kText, font);
            lv_obj_set_width(row[axis], 50);
            lv_obj_set_style_text_align(row[axis], LV_TEXT_ALIGN_RIGHT, 0);
            lv_label_set_long_mode(row[axis], LV_LABEL_LONG_CLIP);
        }
    };
    panel(metrics, 9, 65, 238, 38, 0x333024, 3);
    text(metrics, "总 RMS", 12, 76, kAccent);
    numbers(_rms, 75, &lv_font_montserrat_16);
    for (unsigned band = 0; band < kRmsBandCount; ++band) {
        const int y = 113 + 28 * band;
        char range[20];
        std::snprintf(range, sizeof(range), "%u-%u Hz", kRmsBandEdgesHz[band], kRmsBandEdgesHz[band + 1]);
        text(metrics, range, 12, y + 1, kMuted, &lv_font_montserrat_12);
        numbers(_band_rms[band], y, &lv_font_montserrat_14);
    }

    text(metrics, "总 RMS 趋势", 12, 230);
    _trend_scale = text(metrics, "", 158, 233, kMuted, &lv_font_montserrat_12);
    _trend_chart = trend_canvas(metrics, 12, 254, 234, 74, _trend_canvas, true);
    auto* hit = _sidebar_trend_hits[0] = panel(metrics, 10, 227, 238, 122);
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
    border(hit, kAccent);
    lv_obj_set_style_border_opa(hit, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        hit, [](lv_event_t* e) { static_cast<Impl*>(lv_event_get_user_data(e))->toggle_compass(); }, LV_EVENT_CLICKED, this);
    _sidebar_trend_ticks[0] = text(metrics, "-60 s", 12, 332, kMuted, &lv_font_montserrat_12);
    text(metrics, "现在", 213, 330, kMuted);

    const char* labels[] = {"峰值 / g", "主频 / Hz", "峰值因子", "峭度"};
    std::array<lv_obj_t*, 3>* rows[] = {&_peak, &_frequency, &_crest, &_kurtosis};
    for (unsigned row = 0; row < 4; ++row) {
        const int y = 365 + 24 * row;
        text(metrics, labels[row], 12, y, kMuted);
        numbers(*rows[row], y + 1, &lv_font_montserrat_14);
    }

    text(vector, "合成振动", 12, 42, kText);
    text(vector, "|a|", 221, 42, kAccent);
    auto* summary = panel(vector, 9, 65, 238, 64, 0x333024, 5);
    text(summary, "总 RMS", 12, 23, kAccent);
    _resultant_rms = text(summary, "--", 79, 12, kText, &lv_font_montserrat_32);
    lv_obj_set_width(_resultant_rms, 126);
    lv_obj_set_style_text_align(_resultant_rms, LV_TEXT_ALIGN_RIGHT, 0);
    text(summary, "g", 218, 29, kMuted);
    const auto metric = [&](const char* label, int y) {
        text(vector, label, 12, y, kMuted);
        auto* value = text(vector, "--", 135, y, kText, &lv_font_montserrat_16);
        lv_obj_set_width(value, 111);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        return value;
    };
    for (unsigned band = 0; band < kRmsBandCount; ++band) {
        char range[20];
        std::snprintf(range, sizeof(range), "%u-%u Hz", kRmsBandEdgesHz[band], kRmsBandEdgesHz[band + 1]);
        _resultant_bands[band] = metric(range, 141 + 25 * band);
    }
    text(vector, "总 RMS 趋势", 12, 247);
    _resultant_scale = text(vector, "g", 158, 249, kMuted, &lv_font_montserrat_12);
    _resultant_trend = trend_canvas(vector, 12, 273, 234, 72, _resultant_canvas, true);
    auto* vector_hit = _sidebar_trend_hits[1] = panel(vector, 10, 244, 238, 125);
    lv_obj_set_style_bg_opa(vector_hit, LV_OPA_TRANSP, 0);
    border(vector_hit, kAccent);
    lv_obj_set_style_border_opa(vector_hit, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(vector_hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(vector_hit, [](lv_event_t* e) {
        static_cast<Impl*>(lv_event_get_user_data(e))->toggle_compass();
    }, LV_EVENT_CLICKED, this);
    _sidebar_trend_ticks[1] = text(vector, "-60 s", 12, 350, kMuted, &lv_font_montserrat_12);
    text(vector, "现在", 213, 348, kMuted);
    for (unsigned row = 0; row < 4; ++row) _resultant_details[row] = metric(labels[row], 375 + 25 * row);
}

void VibescopeView::Impl::build_status()
{
    auto* bar = _status_bar = panel(_screen->get(), content_left(), 0, kWidth - content_left(), 48);
    border(bar);
    _compact_logo = panel(bar, 0, 0, 144, 28);
    auto* compact_image = lv_image_create(_compact_logo);
    lv_image_set_src(compact_image, &vn_logo);
    lv_image_set_pivot(compact_image, 0, 0);
    lv_image_set_scale(compact_image, 160); // 228x43 lockup -> approximately 143x27.
    lv_obj_set_pos(compact_image, 0, 0);
    _status        = text(bar, "STOP", 0, 0, kAccent, &vn_font_18);
    _temperature   = text(bar, "TEMP -- ℃", 0, 0, kMuted, &vn_font_18);
    _record_dot = panel(bar, 0, 0, 7, 7, kMuted, LV_RADIUS_CIRCLE);
    _record_status = text(bar, "REC", 0, 0, kMuted, &vn_font_18);
    _clock         = text(bar, "---------- --:--:--", 0, 0, kText, &vn_font_18);
    auto* right = panel(bar, 0, 0, 234, 46);
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, 16, 0);
    lv_obj_add_flag(right, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(right, [](lv_event_t* event) {
        static_cast<Impl*>(lv_event_get_user_data(event))->show_device_settings();
    }, LV_EVENT_CLICKED, this);
    _keyboard_icon = panel(right, 0, 0, 32, 36);
    lv_obj_center(text(_keyboard_icon, LV_SYMBOL_KEYBOARD, 0, 0, kAccent, &lv_font_montserrat_24));
    lv_obj_add_flag(_keyboard_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(_keyboard_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_keyboard_icon, [](lv_event_t* event) {
        static_cast<Impl*>(lv_event_get_user_data(event))->show_keyboard_help();
    }, LV_EVENT_CLICKED, this);
    const char* symbols[] = {LV_SYMBOL_USB, "\xef\x9f\x82", LV_SYMBOL_BATTERY_EMPTY};
    for (unsigned i = 0; i < _status_icons.size(); ++i) {
        auto& icon = _status_icons[i];
        icon.root = panel(right, 0, 0, i == 2 ? 82 : 32, 36);
        lv_obj_remove_flag(icon.root, LV_OBJ_FLAG_CLICKABLE);
        icon.glyph = text(icon.root, symbols[i], 0, 0, kMuted, &vn_status_icons_24);
        if (i == 2) {
            lv_obj_align(icon.glyph, LV_ALIGN_LEFT_MID, 0, 0);
            _battery_percent = text(icon.root, "--", 0, 0, kMuted, &lv_font_montserrat_16);
            lv_obj_align(_battery_percent, LV_ALIGN_LEFT_MID, 36, 0);
        } else {
            lv_obj_center(icon.glyph);
        }
        icon.slash = text(icon.root, "/", 0, 0, kText, &lv_font_montserrat_24);
        lv_obj_center(icon.slash);
        icon.badge = text(icon.root, "?", 22, 22, kMuted, &lv_font_montserrat_12);
        lv_obj_set_style_bg_color(icon.badge, lv_color_hex(kPanel), 0);
        lv_obj_set_style_bg_opa(icon.badge, LV_OPA_COVER, 0);
        lv_obj_add_flag(icon.slash, LV_OBJ_FLAG_HIDDEN);
    }
    layout_status();
    update_status_icons();
}

void VibescopeView::Impl::layout_status()
{
    if (_sidebar_visible) lv_obj_add_flag(_compact_logo, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(_compact_logo, LV_OBJ_FLAG_HIDDEN);
    const int offset = _sidebar_visible ? 0 : 160;
    // The lockup's white wordmark sits below its taller V symbol. Compensate
    // optically so its lettering shares the status labels' visual center.
    lv_obj_align(_compact_logo, LV_ALIGN_LEFT_MID, 14, -3);
    lv_obj_align(_status, LV_ALIGN_LEFT_MID, 14 + offset, 0);
    lv_obj_align(_temperature, LV_ALIGN_LEFT_MID, 104 + offset, 0);
    lv_obj_align(_record_dot, LV_ALIGN_LEFT_MID, 282 + offset, 0);
    lv_obj_align(_record_status, LV_ALIGN_LEFT_MID, 297 + offset, 0);
    lv_obj_align(_clock, LV_ALIGN_CENTER, _sidebar_visible ? 0 : 34, 0);
}

void VibescopeView::Impl::build_footer()
{
    auto* bar = _footer_bar = panel(_screen->get(), 0, 654, kWidth, 66);
    border(bar);
    _sidebar_toggle = lv_button_create(bar);
    surface(_sidebar_toggle, kPanel);
    position(_sidebar_toggle, 10, 11, 44, 44);
    lv_obj_set_style_bg_opa(_sidebar_toggle, LV_OPA_TRANSP, 0);
    auto* frame = panel(_sidebar_toggle, 10, 13, 24, 18, kPanel, 3);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
    border(frame, kMuted);
    _sidebar_divider = panel(_sidebar_toggle, 17, 14, 1, 16, kAccent);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(_sidebar_divider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_sidebar_toggle, [](lv_event_t* event) {
        static_cast<Impl*>(lv_event_get_user_data(event))->toggle_sidebar();
    }, LV_EVENT_CLICKED, this);
    _run = button(bar, "运行", 64, 11, 100, 44,
        [this] { set_acquisition_running(!_snapshot.running && !_snapshot.armed); }, kAccent);
    auto* layout = _layout_dropdown = dropdown(bar, "单图\n双图\n三分图", 936, 11, 100, [this](unsigned value) {
        if (_compass_mode) {
            lv_dropdown_set_selected(_layout_dropdown, _layout_count - 1);
            return;
        }
        _layout_count = value + 1;
        _selected = std::min(_selected, _layout_count - 1);
        layout_plots();
    });
    lv_dropdown_set_symbol(layout, nullptr);
    lv_obj_set_style_text_align(layout, LV_TEXT_ALIGN_CENTER, 0);
    lv_dropdown_set_selected(layout, _layout_count - 1);
    _cursor_button = button(bar, "游标", 296, 11, 100, 44, [] {});
    lv_obj_add_event_cb(_cursor_button->get(), [](lv_event_t* e) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(e));
        auto& config = self->cursor_config();
        config.cursor = !config.cursor;
        self->select(self->_selected);
    }, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_add_event_cb(_cursor_button->get(), [](lv_event_t* e) {
        lv_indev_wait_release(lv_event_get_indev(e));
        static_cast<Impl*>(lv_event_get_user_data(e))->show_cursor_config();
    }, LV_EVENT_LONG_PRESSED, this);
    _trigger_button = button(bar, "触发", 180, 11, 100, 44, [] {});
    lv_obj_set_style_bg_color(_trigger_button->get(), lv_color_hex(kAccent), LV_STATE_CHECKED);
    lv_obj_add_event_cb(_trigger_button->get(), [](lv_event_t* e) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(e));
        if (self->_snapshot.armed) self->set_acquisition_running(false);
        else {
            self->_hold_waveforms = false;
            unsigned span_ms = 100;
            for (unsigned i = 0; i < self->_layout_count; ++i) {
                const auto& config = self->_configs[i];
                if (config.kind == PlotKind::Time || config.kind == PlotKind::Gyro)
                    span_ms = std::max<unsigned>(span_ms, config.span_ms);
            }
            self->_runtime.arm(self->_trigger_threshold / 100.F, span_ms, self->_trigger_position);
            self->_snapshot.armed = true;
            self->_snapshot.trigger_position = self->_trigger_position;
            self->_snapshot.trigger_time_us = 0;
            set_text(self->_status, "TRIGGER");
            set_text(self->_run->label().get(), "停止");
        }
    }, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_add_event_cb(_trigger_button->get(), [](lv_event_t* e) {
        lv_indev_wait_release(lv_event_get_indev(e));
        static_cast<Impl*>(lv_event_get_user_data(e))->show_trigger_config();
    }, LV_EVENT_LONG_PRESSED, this);
    _record = button(bar, "录制", 412, 11, 100, 44, [] {});
    _screenshot_button = button(bar, "截屏", 528, 11, 100, 44, [] {});
    auto* screenshot = _screenshot_button->get();
    for (auto* control : {_record->get(), screenshot}) {
        lv_obj_add_flag(control, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(control, file_button_event, LV_EVENT_ALL, this);
    }
    lv_obj_set_style_bg_color(screenshot, lv_color_hex(kAccent), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(screenshot, lv_color_hex(kAccent), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(screenshot, lv_color_hex(kText), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(screenshot, lv_color_hex(kOnAccent), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(screenshot, lv_color_hex(kOnAccent), LV_STATE_CHECKED);
    lv_obj_remove_local_style_prop(_screenshot_button->label().get(), LV_STYLE_TEXT_COLOR, LV_PART_MAIN);
    const char* navigation[] = {"+", "-", "Fit"};
    for (unsigned action = 0; action < 3; ++action) {
        auto* control = _navigation[action] = lv_button_create(bar);
        surface(control, kSoft, 5);
        lv_obj_set_style_bg_color(control, lv_color_hex(kAccent), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(control, lv_color_hex(kText), 0);
        lv_obj_set_style_text_color(control, lv_color_hex(kOnAccent), LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(control, lv_color_hex(kPanel), LV_STATE_DISABLED);
        lv_obj_set_style_text_color(control, lv_color_hex(0x747474), LV_STATE_DISABLED);
        position(control, 0, 0, 64, 44);
        lv_obj_align(control, LV_ALIGN_RIGHT_MID, -12 - static_cast<int>(2 - action) * 76, 0);
        auto* label = text(control, navigation[action], 0, 0);
        lv_obj_remove_local_style_prop(label, LV_STYLE_TEXT_COLOR, LV_PART_MAIN);
        lv_obj_center(label);
        lv_obj_add_event_cb(control, [](lv_event_t* event) {
            auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
            for (unsigned n = 0; n < 3; ++n)
                if (self->_navigation[n] == lv_event_get_current_target_obj(event))
                    self->navigate(self->_selected, n);
        }, LV_EVENT_CLICKED, this);
    }
}

void VibescopeView::Impl::file_button_event(lv_event_t* event)
{
    const auto code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESS_LOST &&
        code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;

    auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
    auto* button = lv_event_get_current_target_obj(event);
    if (code == LV_EVENT_PRESSED) {
        self->_file_pressed = button;
        self->_press_at = lv_tick_get();
        return;
    }
    if (self->_file_pressed != button) return;
    if (code == LV_EVENT_PRESS_LOST || !released_inside(event)) {
        lv_obj_remove_state(button, LV_STATE_PRESSED);
        self->_file_pressed = nullptr;
        return;
    }

    const bool record = button == self->_record->get();
    if (code == LV_EVENT_PRESSING && lv_tick_elaps(self->_press_at) >= 1000) {
        lv_obj_remove_state(button, LV_STATE_PRESSED);
        self->_file_pressed = nullptr;
        if (auto* input = lv_event_get_indev(event)) lv_indev_wait_release(input);
        // Create the modal after LVGL has finished processing the current press.
        self->_file_manager_request = button;
    } else if (code == LV_EVENT_RELEASED) {
        self->_file_pressed = nullptr;
        if (lv_tick_elaps(self->_press_at) >= 1000) {
            self->_file_manager_request = button;
        } else if (record) {
            self->toggle_recording();
        } else {
            self->capture_screenshot();
        }
    }
}

void VibescopeView::Impl::toggle_recording()
{
    _record_intent = !_record_intent;
    _runtime.record(_record_intent);
}

void VibescopeView::Impl::capture_screenshot()
{
    if (lv_obj_has_state(_screenshot_button->get(), LV_STATE_CHECKED) ||
        !lv_obj_has_flag(_screenshot_flash, LV_OBJ_FLAG_HIDDEN)) return;
    auto* display = lv_obj_get_display(_screen->get());
    ScreenshotFrame frame;
    frame.width = static_cast<unsigned>(lv_display_get_horizontal_resolution(display));
    frame.height = static_cast<unsigned>(lv_display_get_vertical_resolution(display));
    const auto bytes = static_cast<std::size_t>(frame.width) * frame.height * sizeof(std::uint16_t);
    const void* pixels = nullptr;
#if defined(PLATFORM_BUILD_DESKTOP)
    const auto* buffer = lv_display_get_buf_active(display);
    if (buffer && buffer->header.cf == LV_COLOR_FORMAT_RGB565 &&
        buffer->header.stride == frame.width * sizeof(std::uint16_t) && buffer->data_size >= bytes)
        pixels = buffer->data;
#else
    std::size_t size = 0;
    pixels = lvgl_port_get_dpi_framebuffer(display, &size);
    if (size != bytes) pixels = nullptr;
    frame.native_portrait = true;
#endif
    if (pixels) frame.pixels.reset(static_cast<std::uint16_t*>(std::malloc(bytes)));
    if (!frame.pixels) {
        ui::pop_a_toast("截屏失败", ui::toast_type::error, 1800, &vn_font_16, 0);
        return;
    }
    std::memcpy(frame.pixels.get(), pixels, bytes);
    if (!_runtime.request_screenshot(std::move(frame))) {
        ui::pop_a_toast("SD 文件操作中", ui::toast_type::warning, 1800, &vn_font_16, 0);
        return;
    }
    lv_obj_add_state(_screenshot_button->get(), LV_STATE_CHECKED);
}

void VibescopeView::Impl::toggle_sidebar()
{
    if (_compass_mode && _sidebar_visible) toggle_compass();
    _sidebar_visible = !_sidebar_visible;
    layout_sidebar();
}

void VibescopeView::Impl::layout_sidebar()
{
    if (_sidebar_visible) lv_obj_remove_flag(_sidebar, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(_sidebar, LV_OBJ_FLAG_HIDDEN);
    const int left = content_left();
    position(_status_bar, left, 0, kWidth - left, 48);
    layout_status();
    lv_obj_set_style_bg_color(_sidebar_divider, lv_color_hex(_sidebar_visible ? kAccent : kMuted), 0);
    layout_plots();
}

void VibescopeView::Impl::set_status_icon(StatusIcon& icon, hal::ConnectionState state)
{
    if (icon.state == static_cast<int>(state)) return;
    icon.state = static_cast<int>(state);
    using hal::ConnectionState;
    const auto color = state == ConnectionState::Connected ? kAccent : state == ConnectionState::Fault ? kRed : kMuted;
    set_text_color(icon.glyph, color);
    set_text_color(icon.badge, color);
    lv_obj_set_state(icon.root, LV_STATE_DISABLED, false);
    lv_obj_set_style_opa(icon.glyph, state == ConnectionState::Off ? LV_OPA_50 : LV_OPA_COVER, 0);
    if (state == ConnectionState::Off) lv_obj_remove_flag(icon.slash, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(icon.slash, LV_OBJ_FLAG_HIDDEN);
    set_text(icon.badge, state == ConnectionState::Connected ? LV_SYMBOL_OK :
                         state == ConnectionState::Connecting ? LV_SYMBOL_REFRESH :
                         state == ConnectionState::Fault ? LV_SYMBOL_WARNING :
                         state == ConnectionState::Idle ? "o" : "?");
    if (state == ConnectionState::Off) lv_obj_add_flag(icon.badge, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(icon.badge, LV_OBJ_FLAG_HIDDEN);
}

void VibescopeView::Impl::update_status_icons()
{
    using hal::ConnectionState;
    set_status_icon(_status_icons[0], GetHAL()->getComputerUsbState());
    const auto sd = GetHAL()->getSdCardState();
    set_status_icon(_status_icons[1], _snapshot.recording && sd == ConnectionState::Connected ?
        ConnectionState::Connecting : sd);
    if (_snapshot.recording && sd == ConnectionState::Connected) {
        set_text_color(_status_icons[1].glyph, 0xff4d4f);
        set_text_color(_status_icons[1].badge, 0xff4d4f);
    }
    const auto power = GetHAL()->getPowerStatus();
    const bool present = power.valid && power.battery == hal::BatteryPresence::Present;
    const auto percent = present ? hal::battery_percent(power.voltage) : 0U;
    const int key = (static_cast<int>(power.battery) * 32 + power.valid * 16 + power.charging * 8 + power.level) * 101
                    + percent;
    if (_power_icon_key == key) return;
    _power_icon_key = key;
    char percent_text[8] = "--";
    if (present) std::snprintf(percent_text, sizeof(percent_text), "%u%%", percent);
    set_text(_battery_percent, percent_text);
    set_text_color(_battery_percent, present ? kText : kMuted);
    auto& icon = _status_icons[2];
    lv_obj_set_style_text_font(icon.badge, &lv_font_montserrat_12, 0);
    lv_obj_set_align(icon.badge, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(icon.badge, 22, 22);
    set_text_color(icon.badge, kMuted);
    lv_obj_add_flag(icon.slash, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(icon.badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(icon.glyph, LV_OPA_COVER, 0);
    set_text_color(icon.glyph, kText);
    if (!power.valid || power.battery == hal::BatteryPresence::Unknown) {
        set_text(icon.glyph, LV_SYMBOL_BATTERY_EMPTY);
        set_text(icon.badge, "?");
        lv_obj_remove_flag(icon.badge, LV_OBJ_FLAG_HIDDEN);
        set_text_color(icon.glyph, kMuted);
        return;
    }
    if (power.battery == hal::BatteryPresence::Absent) {
        set_text(icon.glyph, "\xef\x87\xa6"); // FA power plug (external supply, no battery).
        return;
    }
    const char* levels[] = {LV_SYMBOL_BATTERY_EMPTY, LV_SYMBOL_BATTERY_1, LV_SYMBOL_BATTERY_2,
                            LV_SYMBOL_BATTERY_3, LV_SYMBOL_BATTERY_FULL};
    set_text(icon.glyph, power.charging ? LV_SYMBOL_BATTERY_EMPTY : levels[std::min<unsigned>(power.level, 4)]);
    if (power.charging) {
        set_text(icon.badge, LV_SYMBOL_CHARGE);
        lv_obj_set_style_text_font(icon.badge, &vn_status_icons_24, 0);
        lv_obj_align(icon.badge, LV_ALIGN_LEFT_MID, 7, -1);
        set_text_color(icon.badge, kAccent);
        lv_obj_remove_flag(icon.badge, LV_OBJ_FLAG_HIDDEN);
    } else if (!power.level) set_text_color(icon.glyph, kRed);
}

}  // namespace vibescope_view
