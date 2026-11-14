/**
 * @file view_compass.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-11
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "view_private.h"
#include "pose_geometry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace vibescope_view {
namespace {
constexpr int kPanelTop = 56, kPanelHeight = 590;
constexpr int kInset = 12, kHeaderHeight = 38, kCanvasSize = 216;
constexpr int kStrengthHeight = 274, kModelHeight = kStrengthHeight - kHeaderHeight - kInset;
constexpr int kTrendTop = kStrengthHeight + 8, kTrendHeight = kPanelHeight - kTrendTop;
constexpr int kTrendLeft = 60, kTrendPlotTop = 68, kTrendPlotHeight = 188;
constexpr float kAxisLength = 2.65F;

PoseProjection compass_projection(const Axes& angles)
{
    return {kCanvasSize * .5F, kCanvasSize * .5F, 33.F, angles[0], angles[1], angles[2] - .30F};
}

std::uint16_t shade(unsigned color, lv_opa_t opacity)
{
    return lv_color_to_u16(lv_color_mix(lv_color_hex(color), lv_color_hex(kPlot), opacity));
}

int edge(lv_point_t a, lv_point_t b, lv_point_t p)
{
    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
}

// Projected points stay inside the square: the longest axis is under 88 px.
void fill_triangle(std::uint16_t* pixels, lv_point_t a, lv_point_t b, lv_point_t c, std::uint16_t color)
{
    const int direction = edge(a, b, c);
    if (!direction) return;
    if (direction < 0) std::swap(b, c);
    const int left = std::min({a.x, b.x, c.x}), right = std::max({a.x, b.x, c.x});
    const int top = std::min({a.y, b.y, c.y}), bottom = std::max({a.y, b.y, c.y});
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            const lv_point_t p{x, y};
            if (edge(a, b, p) >= 0 && edge(b, c, p) >= 0 && edge(c, a, p) >= 0)
                pixels[y * kCanvasSize + x] = color;
        }
}

void draw_stroke(std::uint16_t* pixels, lv_point_t a, lv_point_t b, std::uint16_t color)
{
    scope_raster_line(pixels, kCanvasSize, kCanvasSize, a.x, a.y, b.x, b.y, color);
}

void draw_lobe(std::uint16_t* pixels, lv_point_t center, lv_point_t tip, unsigned color)
{
    const float dx = tip.x - center.x, dy = tip.y - center.y;
    const float length = std::hypot(dx, dy);
    if (length < 3.F) return;

    const float shoulder_x = center.x + dx * .37F, shoulder_y = center.y + dy * .37F;
    for (const bool core : {false, true}) {
        const float width = core ? std::min(length * .10F, 12.F) : std::min(length * .22F, 27.F);
        const float nx = -dy * width / length, ny = dx * width / length;
        const lv_point_t a{static_cast<int>(shoulder_x + nx), static_cast<int>(shoulder_y + ny)};
        const lv_point_t b{static_cast<int>(shoulder_x - nx), static_cast<int>(shoulder_y - ny)};
        const auto fill = shade(color, core ? LV_OPA_60 : LV_OPA_30);
        fill_triangle(pixels, center, a, b, fill);
        fill_triangle(pixels, tip, a, b, fill);
    }
    draw_stroke(pixels, center, tip, shade(color, LV_OPA_90));
}
}

void VibescopeView::Impl::bind_pose_reset(lv_obj_t* object)
{
    lv_obj_add_event_cb(object, [](lv_event_t* event) {
        static_cast<Impl*>(lv_event_get_user_data(event))->reset_pose();
    }, LV_EVENT_SHORT_CLICKED, this);
}

void VibescopeView::Impl::reset_pose()
{
    _pose_flash.teleport(LV_OPA_50);
    _pose_flash.play();
    _pose_flash = 0;
    lv_obj_set_style_bg_opa(_pose_hit, LV_OPA_50, 0);
    if (!_snapshot.connected || !_snapshot.attitude_valid) {
        ui::pop_a_toast("暂无有效姿态", ui::toast_type::warning, 1600, &vn_font_16, 0);
        return;
    }
    _reference = _snapshot.attitude;
    for (auto& angle : _angle_anim) angle.teleport(0);
    _pose_rendered_initialized = false;
    render_pose();
    if (_compass_mode) render_compass();
}

void VibescopeView::Impl::toggle_compass()
{
    _compass_mode = !_compass_mode;
    lv_obj_set_state(_layout_dropdown, LV_STATE_DISABLED, _compass_mode);
    for (auto* hit : _sidebar_trend_hits)
        lv_obj_set_style_border_opa(hit, _compass_mode ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if (_compass_mode) {
        if (!_sidebar_visible) {
            _sidebar_visible = true;
            layout_sidebar();
        } else {
            layout_compass();
        }
        render_pose();
        render_compass();
    } else {
        lv_obj_add_flag(_compass, LV_OBJ_FLAG_HIDDEN);
        _plots[_selected].rendered_valid = false;
        for (unsigned i = 0; i < _layout_count; ++i) lv_obj_remove_flag(_plots[i].panel, LV_OBJ_FLAG_HIDDEN);
    }
    select(_selected);
}

void VibescopeView::Impl::layout_compass()
{
    const int left = content_left() + 8;
    const int width = kWidth - left - 8;
    const int model_width = (width - 3 * kInset) * 3 / 5;
    const int summary_width = width - model_width - 3 * kInset;
    if (!_compass) {
        _compass = panel(_screen->get(), left, kPanelTop, width, kPanelHeight, kBackground);
        _compass_strength = panel(_compass, 0, 0, width, kStrengthHeight, kPlot, 5);
        border(_compass_strength);
        auto* heading = panel(_compass_strength, 1, 1, width - 2, kHeaderHeight - 1, kPanel, 4);
        text(heading, "振动强度", 10, 8, kText, &vn_font_18);
        _compass_model = panel(_compass_strength, kInset, kHeaderHeight, model_width, kModelHeight, kPlot, 5);
        lv_obj_add_flag(_compass_model, LV_OBJ_FLAG_CLICKABLE);
        bind_pose_reset(_compass_model);
        constexpr auto bytes = kCanvasSize * kCanvasSize * sizeof(std::uint16_t);
#if defined(ESP_PLATFORM)
        _compass_pixels.reset(static_cast<std::uint16_t*>(heap_caps_aligned_alloc(64, bytes,
                                                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
#else
        _compass_pixels.reset(static_cast<std::uint16_t*>(std::malloc(bytes)));
#endif
        if (_compass_pixels) {
            _compass_canvas = lv_canvas_create(_compass_model);
            lv_canvas_set_buffer(_compass_canvas, _compass_pixels.get(), kCanvasSize, kCanvasSize,
                                 LV_COLOR_FORMAT_RGB565);
            lv_obj_remove_flag(_compass_canvas, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_center(_compass_canvas);
        } else {
            lv_obj_center(text(_compass_model, "模型内存不足", 0, 0, kMuted));
        }
        if (_compass_canvas)
            for (unsigned i = 0; i < 3; ++i)
                _compass_axis_labels[i] = text(_compass_model, axis_names[i], 0, 0, kAxes[i],
                                               &lv_font_montserrat_24);

        _compass_summary = panel(_compass_strength, model_width + 2 * kInset, kHeaderHeight, summary_width,
                                 kModelHeight, kPlot);
        text(_compass_summary, "最强振动轴", 10, 24, kMuted, &vn_font_18);
        _compass_strongest = text(_compass_summary, "--", 125, 12, kMuted, &lv_font_montserrat_36);
        for (unsigned i = 0; i < 3; ++i) {
            const int y = 76 + 40 * i;
            text(_compass_summary, axis_names[i], 10, y, kAxes[i], &lv_font_montserrat_24);
            _compass_values[i] = text(_compass_summary, "--", 0, 0, kText, &lv_font_montserrat_18);
            lv_obj_align(_compass_values[i], LV_ALIGN_TOP_RIGHT, -12, y + 3);
            auto* bar = _compass_bars[i] = lv_bar_create(_compass_summary);
            lv_obj_remove_style_all(bar);
            lv_obj_set_style_bg_color(bar, lv_color_hex(kLine), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
            lv_obj_set_style_bg_color(bar, lv_color_hex(kAxes[i]), LV_PART_INDICATOR);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
            lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
            lv_bar_set_range(bar, 0, 1000);
        }
    }
    position(_compass, left, kPanelTop, width, kPanelHeight);
    lv_obj_set_width(_compass_strength, width);
    lv_obj_set_width(lv_obj_get_child(_compass_strength, 0), width - 2);
    position(_compass_model, kInset, kHeaderHeight, model_width, kModelHeight);
    position(_compass_summary, model_width + 2 * kInset, kHeaderHeight, summary_width, kModelHeight);
    for (unsigned i = 0; i < 3; ++i) position(_compass_bars[i], 42, 87 + 40 * i, summary_width - 155, 8);
    layout_rms_trend(width);
    lv_obj_remove_flag(_compass, LV_OBJ_FLAG_HIDDEN);
    for (unsigned i = 0; i < _layout_count; ++i) lv_obj_add_flag(_plots[i].panel, LV_OBJ_FLAG_HIDDEN);
    for (auto* control : _navigation) lv_obj_add_state(control, LV_STATE_DISABLED);
    lv_obj_update_layout(_compass);
    _compass_drawn = false;
}

void VibescopeView::Impl::layout_rms_trend(int width)
{
    const int plot_width = width - kTrendLeft - 20;
    if (!_rms_panel) {
        _rms_panel = panel(_compass, 0, kTrendTop, width, kTrendHeight, kPlot, 5);
        border(_rms_panel);
        auto* heading = panel(_rms_panel, 1, 1, width - 2, kHeaderHeight - 1, kPanel, 4);
        text(heading, "总 RMS 趋势", 10, 8, kText, &vn_font_18);
        _rms_source = text(heading, "", 0, 0, kMuted, &lv_font_montserrat_14);
        lv_obj_align(_rms_source, LV_ALIGN_RIGHT_MID, -14, 0);

        text(_rms_panel, "RMS / g", 10, 45, kMuted, &lv_font_montserrat_14);
        _rms_latest = text(_rms_panel, "-- g", 0, 0, kAccent, &lv_font_montserrat_18);
        lv_obj_align(_rms_latest, LV_ALIGN_TOP_RIGHT, -20, 43);
        for (unsigned i = 0; i < 5; ++i) {
            _rms_value_ticks[i] = text(_rms_panel, "", 7, kTrendPlotTop + i * (kTrendPlotHeight - 1) / 4 - 7,
                                       kMuted, &lv_font_montserrat_12);
            lv_obj_set_width(_rms_value_ticks[i], kTrendLeft - 14);
            lv_obj_set_style_text_align(_rms_value_ticks[i], LV_TEXT_ALIGN_RIGHT, 0);
            _rms_time_ticks[i] = text(_rms_panel, "", 0, kTrendPlotTop + kTrendPlotHeight + 4, kMuted);
            lv_obj_set_width(_rms_time_ticks[i], 66);
            lv_obj_set_style_text_align(_rms_time_ticks[i], i == 0 ? LV_TEXT_ALIGN_LEFT :
                                       i == 4 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER, 0);
        }
        for (unsigned i = 0; i < 2; ++i) {
            const unsigned color = i ? kAxes[2] : kAxes[0];
            _rms_cursor_lines[i] = panel(_rms_panel, kTrendLeft, kTrendPlotTop, 1, kTrendPlotHeight, color);
            _rms_cursor_tags[i] = text(_rms_panel, i ? "B" : "A", kTrendLeft, kTrendPlotTop + 4, color,
                                       &lv_font_montserrat_12);
        }
        _rms_cursor_value = text(_rms_panel, "", kTrendLeft, kTrendPlotTop + kTrendPlotHeight + 25, kText,
                                 &lv_font_montserrat_14);
        _rms_hit = panel(_rms_panel, kTrendLeft, kTrendPlotTop, plot_width, kTrendPlotHeight);
        lv_obj_set_style_bg_opa(_rms_hit, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(_rms_hit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(_rms_hit, rms_plot_event, LV_EVENT_ALL, this);
    }
    lv_obj_set_width(_rms_panel, width);
    lv_obj_set_width(lv_obj_get_parent(_rms_source), width - 2);
    if (_rms_canvas.width != plot_width) {
        if (_rms_plot) lv_obj_delete(_rms_plot);
        _rms_plot = trend_canvas(_rms_panel, kTrendLeft, kTrendPlotTop, plot_width, kTrendPlotHeight,
                                 _rms_canvas, true);
        lv_obj_move_to_index(_rms_plot, 0);
    }
    lv_obj_set_width(_rms_hit, plot_width);
    for (unsigned i = 0; i < 5; ++i)
        lv_obj_set_x(_rms_time_ticks[i], kTrendLeft + i * (plot_width - 1) / 4 -
                                        (i == 0 ? 0 : i == 4 ? 66 : 33));
}

void VibescopeView::Impl::render_rms_trend()
{
    const auto& history = _rms_history[_metric_source_index][kResultantChannel];
    const auto span = kRmsTrendWindowsSeconds[_rms_trend_window];
    trend(_rms_plot, _rms_canvas, _rms_history[_metric_source_index], kResultantChannels, kPlot);
    set_text(_rms_source, source_names[_metric_source_index]);
    char value[32];
    if (history.empty() || history.back() < 0.F) set_text(_rms_latest, "-- g");
    else {
        std::snprintf(value, sizeof(value), "%.3f g", history.back());
        set_text(_rms_latest, value);
    }
    for (unsigned i = 0; i < 5; ++i) {
        const float amplitude = _rms_canvas.maximum * (4 - i) / 4;
        if (i == 4) {
            set_text(_rms_value_ticks[i], "0");
            set_text(_rms_time_ticks[i], "现在");
            continue;
        }
        std::snprintf(value, sizeof(value), amplitude >= 1.F ? "%.2f" : "%.3f", amplitude);
        set_text(_rms_value_ticks[i], value);
        const unsigned quarters = span * (4 - i);
        std::snprintf(value, sizeof(value), quarters % 4 ? "-%.2f s" : "-%.0f s", quarters / 4.F);
        set_text(_rms_time_ticks[i], value);
    }
    render_rms_cursor();
}

void VibescopeView::Impl::render_rms_cursor()
{
    const auto& config = _rms_cursor;
    const auto& history = _rms_history[_metric_source_index][kResultantChannel];
    const auto span = kRmsTrendWindowsSeconds[_rms_trend_window];
    const unsigned count = config.cursor ? (config.dual_cursor ? 2U : 1U) : 0U;
    unsigned age[2]{};
    float rms[2]{-1.F, -1.F};
    char value[2][24]{};
    for (unsigned i = 0; i < 2; ++i) {
        lv_obj_update_flag(_rms_cursor_lines[i], LV_OBJ_FLAG_HIDDEN, i >= count);
        lv_obj_update_flag(_rms_cursor_tags[i], LV_OBJ_FLAG_HIDDEN, i >= count || count == 1);
        if (i >= count) continue;
        const float fraction = i ? config.cursor_b_fraction : config.cursor_fraction;
        age[i] = std::lround((1.F - fraction) * span);
        const int x = kTrendLeft + (span - age[i]) * (_rms_canvas.width - 1) / span;
        lv_obj_set_x(_rms_cursor_lines[i], x);
        lv_obj_set_x(_rms_cursor_tags[i], std::clamp(x + (i ? 4 : -12), kTrendLeft,
                                                    kTrendLeft + _rms_canvas.width - 12));
        if (age[i] < history.size()) rms[i] = history[history.size() - 1 - age[i]];
        if (rms[i] >= 0.F) std::snprintf(value[i], sizeof(value[i]), "%.3f g", rms[i]);
        else std::snprintf(value[i], sizeof(value[i]), "-- g");
    }

    char label[128]{};
    if (count == 1)
        std::snprintf(label, sizeof(label), "%d s / %s", -static_cast<int>(age[0]), value[0]);
    else if (count == 2) {
        char difference[24] = "-- g";
        if (rms[0] >= 0.F && rms[1] >= 0.F)
            std::snprintf(difference, sizeof(difference), "%.3f g", std::abs(rms[1] - rms[0]));
        std::snprintf(label, sizeof(label), "A %d s / %s    B %d s / %s    dT %u s    dRMS %s",
                      -static_cast<int>(age[0]), value[0], -static_cast<int>(age[1]), value[1],
                      std::max(age[0], age[1]) - std::min(age[0], age[1]), difference);
    }
    set_text(_rms_cursor_value, label);
}

void VibescopeView::Impl::rms_plot_event(lv_event_t* event)
{
    auto& self = *static_cast<Impl*>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
        code != LV_EVENT_LONG_PRESSED && code != LV_EVENT_SHORT_CLICKED) return;
    auto* input = lv_event_get_indev(event);
    if (!input || lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER || self._window) return;

    lv_point_t point;
    lv_indev_get_point(input, &point);
    lv_area_t bounds;
    lv_obj_get_coords(self._rms_hit, &bounds);
    const int plot_width = self._rms_canvas.width - 1;
    const int x = point.x - bounds.x1;
    auto& config = self._rms_cursor;
    if (code == LV_EVENT_PRESSED) {
        self._rms_press_point = point;
        self._rms_dragging = false;
        const int a = std::lround(config.cursor_fraction * plot_width);
        const int b = std::lround(config.cursor_b_fraction * plot_width);
        self._rms_grab = config.dual_cursor && std::abs(x - b) < std::abs(x - a) ? 1 : 0;
        const int offset = x - (self._rms_grab ? b : a);
        self._rms_grab_offset = std::abs(offset) <= 10 ? offset : 0;
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        self._rms_dragging = self._rms_dragging || std::abs(point.x - self._rms_press_point.x) >= 7 ||
                             std::abs(point.y - self._rms_press_point.y) >= 7;
        if (!self._rms_dragging) return;
    } else if (code == LV_EVENT_LONG_PRESSED) {
        if (!self._rms_dragging) {
            lv_indev_wait_release(input);
            self.show_rms_trend_config();
        }
        return;
    } else if (self._rms_dragging || x < 0 || x > plot_width || point.y < bounds.y1 || point.y > bounds.y2) {
        return;
    }
    if (!config.cursor) return;
    auto& fraction = self._rms_grab ? config.cursor_b_fraction : config.cursor_fraction;
    const int offset = code == LV_EVENT_PRESSING ? self._rms_grab_offset : 0;
    fraction = std::clamp(static_cast<float>(x - offset) / plot_width, 0.F, 1.F);
    self.render_rms_cursor();
}

void VibescopeView::Impl::render_compass()
{
    const auto& analysis = _analysis[_metric_source_index];
    Axes rms{};
    bool valid = _snapshot.connected;
    for (unsigned i = 0; i < 3; ++i) {
        valid = valid && analysis[i].valid && std::isfinite(analysis[i].rms) && analysis[i].rms >= 0.F;
        rms[i] = analysis[i].rms;
    }
    if (!valid) rms = {};
    const auto strongest = std::max_element(rms.begin(), rms.end());
    const float peak = *strongest;
    const unsigned axis = strongest - rms.begin();
    float scale = valid ? _compass_scale : .1F;
    while (peak > scale) scale = std::min(scale * 2.F, std::numeric_limits<float>::max());
    while (scale > .1F && peak < scale * .25F) scale *= .5F;

    if (!_compass_drawn || valid != _compass_rms_valid || rms != _compass_rms || scale != _compass_scale) {
        const bool has_strongest = valid && peak > 0.F;
        set_text(_compass_strongest, has_strongest ? axis_names[axis] : "--");
        lv_obj_set_style_text_color(_compass_strongest, lv_color_hex(has_strongest ? kAxes[axis] : kMuted), 0);
        char value[48];
        for (unsigned i = 0; i < 3; ++i) {
            std::snprintf(value, sizeof(value), "%.3f g", rms[i]);
            set_text(_compass_values[i], valid ? value : "--");
            lv_bar_set_value(_compass_bars[i], static_cast<int>(std::lround(1000.F * (rms[i] / scale))), LV_ANIM_OFF);
        }
    }

    const Axes angles = _pose_rendered_valid ? _pose_rendered : Axes{};
    if (_compass_drawn && valid == _compass_rms_valid && rms == _compass_rms && scale == _compass_scale &&
        angles == _compass_angles && _pose_rendered_valid == _compass_pose_valid) return;

    _compass_drawn = true;
    _compass_rms_valid = valid;
    _compass_pose_valid = _pose_rendered_valid;
    _compass_rms = rms;
    _compass_scale = scale;
    _compass_angles = angles;
    if (!_compass_canvas) return;
    const auto project = compass_projection(angles);
    const int offset_x = (lv_obj_get_width(_compass_model) - kCanvasSize) / 2;
    const int offset_y = (kModelHeight - kCanvasSize) / 2;
    for (unsigned i = 0; i < 3; ++i) {
        const auto point = project.axis(i, kAxisLength + .22F);
        lv_obj_set_pos(_compass_axis_labels[i], offset_x + point.x - 8, offset_y + point.y - 13);
    }
    draw_compass();
}

void VibescopeView::Impl::draw_compass()
{
    auto* pixels = _compass_pixels.get();
    std::fill_n(pixels, kCanvasSize * kCanvasSize, lv_color_to_u16(lv_color_hex(kPlot)));
    const auto project = compass_projection(_compass_angles);
    const auto center = project(0, 0, 0);
    for (unsigned i = 0; i < 3; ++i)
        draw_stroke(pixels, project.axis(i, -1.55F), project.axis(i, kAxisLength), shade(kAxes[i], LV_OPA_30));
    for (unsigned i = 0; i < 3; ++i)
        draw_lobe(pixels, center, project.axis(i, (_compass_rms[i] / _compass_scale) * kAxisLength), kAxes[i]);

    lv_point_t corners[8];
    for (unsigned i = 0; i < 8; ++i)
        corners[i] = project(kPoseVertices[i][0], kPoseVertices[i][1], kPoseVertices[i][2]);
    const auto wire = shade(kText, _compass_pose_valid ? LV_OPA_70 : LV_OPA_40);
    for (const auto& edge : kPoseEdges) draw_stroke(pixels, corners[edge[0]], corners[edge[1]], wire);
    lv_obj_invalidate(_compass_canvas);
}
}  // namespace vibescope_view
