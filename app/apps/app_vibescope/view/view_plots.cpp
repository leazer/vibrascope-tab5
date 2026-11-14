/**
 * @file view_plots.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "view_private.h"
#include "pose_geometry.h"

namespace vibescope_view {
namespace {
const auto& waterfall_colors(WaterfallPalette palette)
{
    static constexpr auto colors = [] {
        constexpr unsigned stops[][5] = {
            {0x121212, 0xffc400},
            {0x0d1021, 0x1748bb, 0x25b5db, 0xf6c542, 0xe33037},
            {0x440154, 0x3b528b, 0x21918c, 0x5ec962, 0xfde725},
        };
        std::array<std::array<std::uint16_t, 256>, 3> tables{};
        for (unsigned i = 0; i < tables[0].size(); ++i) {
            for (unsigned map = 0; map < tables.size(); ++map) {
                const unsigned position = map ? i * 4U : i;
                const unsigned section = map ? std::min(position / 255U, 3U) : 0U;
                const unsigned blend = position - section * 255U;
                const auto from = stops[map][section], to = stops[map][section + 1];
                const auto channel = [=](unsigned shift) {
                    return (((from >> shift) & 255U) * (255U - blend) + ((to >> shift) & 255U) * blend) / 255U;
                };
                tables[map][i] = static_cast<std::uint16_t>(((channel(16) & 0xf8U) << 8U) |
                    ((channel(8) & 0xfcU) << 3U) | (channel(0) >> 3U));
            }
        }
        return tables;
    }();
    return colors[static_cast<unsigned>(palette)];
}
}  // namespace

void VibescopeView::Impl::navigate(unsigned index, unsigned action)
{
    if (_compass_mode || _snapshot.running) return;
    auto& plot = _plots[index];
    const auto& config = _configs[index];
    if (config.kind == PlotKind::Waterfall) return;
    const auto source = static_cast<unsigned>(config.source);
    const bool fft = config.kind == PlotKind::Spectrum;
    const auto total = plot_data_count(config);
    if (!_snapshot.valid[source] || !total) return;
    if (fft) {
        const auto band = plot.frequency_band;
        if (action == 2) plot.viewport = band;
        else {
            SampleViewport local{std::max(plot.viewport.first, band.first) - band.first, plot.viewport.count};
            local.zoom(band.count, action == 0 ? .5F : 2.F);
            plot.viewport = {band.first + local.first, local.count};
        }
    } else if (action == 2) {
        plot.viewport.reset(total, fft ? total : static_cast<std::size_t>(_snapshot.rate[source] * config.span_ms / 1000));
        plot.viewport_trigger_time = 0;
    } else
        plot.viewport.zoom(total, action == 0 ? .5F : 2.F);
    select(index);
}

void VibescopeView::Impl::plot_event(lv_event_t* event)
{
    const auto code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED &&
        code != LV_EVENT_PRESS_LOST && code != LV_EVENT_CLICKED) return;
    auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
    auto* target = lv_event_get_current_target_obj(event);
    auto* input = lv_indev_active();
    if (!input) return;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    lv_area_t area;
    lv_obj_get_coords(target, &area);
    for (unsigned j = 0; j < self->_layout_count; ++j) {
        auto& plot = self->_plots[j];
        if (plot.panel != target) continue;
        auto& config = self->_configs[j];
        const int x = point.x - area.x1 - 53;
        const int y = point.y - area.y1 - 65;
        const int width = std::max(1, plot.wave_width - 1);
        const bool cursor_visible = config.cursor && plot.rendered_valid && plot.rendered.valid &&
                                    config.kind != PlotKind::Waterfall;
        auto nearest_cursor = [&] {
            const float fraction = static_cast<float>(x) / width;
            return config.dual_cursor &&
                std::abs(fraction - config.cursor_b_fraction) < std::abs(fraction - config.cursor_fraction) ? 1U : 0U;
        };
        auto cursor_fraction = [&](unsigned marker) -> float& {
            return marker ? config.cursor_b_fraction : config.cursor_fraction;
        };
        if (code == LV_EVENT_PRESSED) {
            plot.pressed = true;
            plot.press_point = point;
            plot.pressed_at = lv_tick_get();
            plot.drag_first = plot.viewport.first;
            plot.moved = false;
            plot.in_wave = x >= 0 && x < plot.wave_width && y >= 0 && y < plot.wave_height;
            plot.cursor_index = nearest_cursor();
            plot.cursor_grab_offset = x - static_cast<int>(width * cursor_fraction(plot.cursor_index));
            plot.cursor_grab = plot.in_wave && cursor_visible && std::abs(plot.cursor_grab_offset) <= 12;
        } else if (code == LV_EVENT_PRESSING && plot.pressed) {
            const int distance = point.x - plot.press_point.x;
            plot.moved |= std::abs(distance) >= 4 || std::abs(point.y - plot.press_point.y) >= 4;
            if (plot.moved && plot.in_wave && cursor_visible) {
                if (self->_selected != j) self->select(j);
                const int cursor_x = x - (plot.cursor_grab ? plot.cursor_grab_offset : 0);
                cursor_fraction(plot.cursor_index) = std::clamp(static_cast<float>(cursor_x) / width, 0.F, 1.F);
                self->render_cursor(j);
            } else if (plot.moved && plot.in_wave && !config.cursor && !self->_snapshot.running &&
                       config.kind != PlotKind::Waterfall) {
                const auto total = self->plot_data_count(config);
                const auto offset = config.kind == PlotKind::Spectrum ? plot.frequency_band.first : 0;
                const auto available = config.kind == PlotKind::Spectrum ? plot.frequency_band.count : total;
                SampleViewport local{std::max(plot.drag_first, offset) - offset, plot.viewport.count};
                local.pan(available, -static_cast<std::int64_t>(distance) *
                    static_cast<std::int64_t>(plot.viewport.count > 1 ? plot.viewport.count - 1 : 0) / width);
                plot.viewport = {offset + local.first, local.count};
                self->render_plot(j);
            } else if (!plot.moved && !plot.cursor_grab && !self->_window && lv_tick_elaps(plot.pressed_at) >= 1000) {
                plot.pressed = false;
                plot.moved = true;
                lv_indev_wait_release(input);
                self->select(j);
                self->show_waveform_config();
            }
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            plot.pressed = false;
            if (code == LV_EVENT_PRESS_LOST) plot.moved = true;
        } else if (code == LV_EVENT_CLICKED && !plot.moved) {
            if (cursor_visible && plot.in_wave)
                cursor_fraction(nearest_cursor()) = std::clamp(static_cast<float>(x) / width, 0.F, 1.F);
            self->select(j);
        }
        break;
    }
}

void VibescopeView::Impl::layout_plots()
{
    const int left = content_left();
    for (unsigned i = 0; i < 3; ++i) {
        auto& plot = _plots[i];
        plot.rendered_valid = false;
        plot.viewport_trigger_time = 0;
        plot.pressed = plot.moved = false;
        if (plot.panel) {
            lv_obj_delete(plot.panel);
            plot.panel = nullptr;
        }
        if (i >= _layout_count) continue;
        const int x = left + 8 + (i == 2 && _layout_count == 3 ? (kWidth - left - 24) / 2 + 8 : 0);
        const int y = 56 + (i > 0 ? 296 : 0);
        const int w = _layout_count == 3 && i > 0 ? (kWidth - left - 24) / 2 : kWidth - left - 16;
        const int h = _layout_count == 1 ? 590 : 286;
        plot.panel  = panel(_screen->get(), x, y, w, h, kPlot, 5);
        border(plot.panel);
        lv_obj_set_style_border_post(plot.panel, true, 0);
        auto* head  = panel(plot.panel, 1, 1, w - 2, 37);
        plot.title  = text(head, "", 10, 10);
        auto* legend = panel(head, 0, 0, 210, 26);
        lv_obj_align(legend, LV_ALIGN_RIGHT_MID, -12, 0);
        lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(legend, 7, 0);
        plot.source = text(legend, "", 0, 0, kMuted, &lv_font_montserrat_12);
        for (unsigned axis = 0; axis < 3; ++axis)
            plot.legend[axis] = text(legend, axis_names[axis], 0, 0, kAxes[axis], &lv_font_montserrat_12);
        plot.legend[3] = text(legend, "|a|", 0, 0, kAccent, &lv_font_montserrat_12);
        lv_obj_add_flag(plot.panel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(head, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(plot.panel, plot_event, LV_EVENT_ALL, this);
        plot.wave_width  = w - 75;
        plot.wave_height = h - 100;
        plot.wave_stride = static_cast<int>(lv_draw_buf_width_to_stride(plot.wave_width, LV_COLOR_FORMAT_RGB565) /
                                            sizeof(std::uint16_t));
        plot.wave_pixels.resize(plot.wave_stride * plot.wave_height);
        plot.wave = lv_canvas_create(plot.panel);
        lv_canvas_set_buffer(plot.wave, plot.wave_pixels.data(), plot.wave_width, plot.wave_height,
                             LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(plot.wave, 53, 65);
        lv_obj_remove_flag(plot.wave, LV_OBJ_FLAG_CLICKABLE);
        plot.image = lv_canvas_create(plot.panel);
        // Expand the small history into the existing full-size RGB565 buffer.
        // LVGL's software image scaler otherwise dominates waterfall frames.
        lv_canvas_set_buffer(plot.image, plot.wave_pixels.data(), plot.wave_width, plot.wave_height,
                             LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(plot.image, 53, 65);
        lv_obj_remove_flag(plot.image, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(plot.image, LV_OBJ_FLAG_HIDDEN);
        plot.empty = text(plot.panel, "等待有效数据", 0, 0, kMuted);
        lv_obj_align(plot.empty, LV_ALIGN_CENTER, 0, 0);
        plot.xunit = text(plot.panel, "ms", w - 29, h - 22, kMuted, &lv_font_montserrat_12);
        plot.yunit = text(plot.panel, "g", 10, 44, kMuted, &lv_font_montserrat_12);
        for (unsigned t = 0; t < 5; ++t) {
            plot.yticks[t] = text(plot.panel, "", 5, 59 + (h - 100) * t / 4, kMuted, &lv_font_montserrat_12);
            plot.xticks[t] = text(plot.panel, "", 53 + (w - 75) * t / 4, h - 31, kMuted, &lv_font_montserrat_12);
        }
        for (unsigned i = 0; i < kMaxSpectrumPeaks; ++i) {
            plot.peak_dots[i] = panel(plot.panel, 0, 0, 5, 5, kAccent, 3);
            plot.peak_labels[i] = text(plot.panel, "", 0, 0, kAccent, &lv_font_montserrat_12);
            lv_obj_set_width(plot.peak_labels[i], 65);
            lv_obj_set_style_text_align(plot.peak_labels[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_bg_color(plot.peak_labels[i], lv_color_hex(kPlot), 0);
            lv_obj_set_style_bg_opa(plot.peak_labels[i], LV_OPA_90, 0);
            lv_obj_remove_flag(plot.peak_dots[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(plot.peak_dots[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(plot.peak_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
        for (unsigned marker = 0; marker < 2; ++marker) {
            const auto color = marker == 0 ? kAccent : kAxes[2];
            plot.cursors[marker] = panel(plot.panel, 53, 65, 1, h - 100, color);
            plot.cursor_tags[marker] = text(plot.panel, marker == 0 ? "A" : "B", 53, 65, color, &lv_font_montserrat_12);
            lv_obj_add_flag(plot.cursors[marker], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(plot.cursor_tags[marker], LV_OBJ_FLAG_HIDDEN);
        }
        plot.cursor_value = text(plot.panel, "", 53, 44, kAccent, &lv_font_montserrat_12);
        lv_obj_add_flag(plot.cursor_value, LV_OBJ_FLAG_HIDDEN);
        plot.trigger_line = panel(plot.panel, 53, 65, 1, plot.wave_height, kRed);
        plot.trigger_marker = panel(plot.panel, 48, 57, 11, 9);
        lv_obj_set_style_bg_opa(plot.trigger_marker, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(plot.trigger_marker, [](lv_event_t* event) {
            lv_area_t coords;
            lv_obj_get_coords(lv_event_get_current_target_obj(event), &coords);
            lv_draw_triangle_dsc_t triangle;
            lv_draw_triangle_dsc_init(&triangle);
            triangle.bg_color = lv_color_hex(kRed);
            triangle.p[0] = {coords.x1, coords.y1};
            triangle.p[1] = {coords.x2, coords.y1};
            triangle.p[2] = {coords.x1 + 5, coords.y2};
            lv_draw_triangle(lv_event_get_layer(event), &triangle);
        }, LV_EVENT_DRAW_MAIN, nullptr);
        for (auto* marker : {plot.trigger_line, plot.trigger_marker}) {
            lv_obj_remove_flag(marker, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_compass_mode) layout_compass();
    select(_selected);
}

bool VibescopeView::Impl::uses_resultant(const PlotConfig& config) const
{
    return config.axis_mode == PlotAxisMode::Resultant ||
           (config.axis_mode == PlotAxisMode::Follow && _resultant_display);
}

const ScopeAnalysisOutput* VibescopeView::Impl::resultant_frame(unsigned source) const
{
    return source == 2 ? _gyro_result.get() : _accepted_results[source].get();
}

const std::vector<float>& VibescopeView::Impl::resultant_samples(unsigned source) const
{
    static const std::vector<float> empty;
    const auto* frame = resultant_frame(source);
    return frame ? frame->resultant_samples[source] : empty;
}

const Analysis& VibescopeView::Impl::plot_analysis(const PlotConfig& config, unsigned axis) const
{
    const auto source = std::min(static_cast<unsigned>(config.source), 1U);
    if (!uses_resultant(config)) return _analysis[source][axis];
    static const Analysis empty;
    const auto* frame = resultant_frame(source);
    return frame ? frame->resultant[source] : empty;
}

std::size_t VibescopeView::Impl::plot_data_count(const PlotConfig& config) const
{
    if (config.kind == PlotKind::Spectrum) return plot_analysis(config, config.axis).spectrum.size();
    const auto source = static_cast<unsigned>(config.source);
    return uses_resultant(config) ? resultant_samples(source).size() : _snapshot.samples[source].size();
}

void VibescopeView::Impl::set_resultant_display(bool enabled)
{
    _resultant_display = enabled;
    lv_obj_update_flag(_xyz_metrics, LV_OBJ_FLAG_HIDDEN, enabled);
    lv_obj_update_flag(_resultant_metrics, LV_OBJ_FLAG_HIDDEN, !enabled);
    _metrics_draw_tick = 0;
    _trend_serial = {};
    _rendered_metric_source = 2;
    for (auto& plot : _plots) {
        plot.rendered_valid = false;
        plot.waterfall_ready = false;
        plot.last_serial = 0;
    }
}

void VibescopeView::Impl::select(unsigned index)
{
    for (unsigned i = 0; i < _configs.size(); ++i) {
        auto& config = _configs[i];
        const auto old_source = config.source;
        config.source = static_cast<Source>(_metric_source_index);
        config.normalize_source();
        config.all_axes = config.kind != PlotKind::Waterfall && !uses_resultant(config) &&
            (config.axis_mode == PlotAxisMode::Follow || config.axis_mode == PlotAxisMode::XYZ);
        config.axis = config.axis_mode == PlotAxisMode::X ? 0 : config.axis_mode == PlotAxisMode::Y ? 1 : 2;
        if (config.source != old_source) {
            _plots[i].waterfall_ready = false;
            _plots[i].last_serial = 0;
        }
    }
    _selected = index;
    auto& p   = _configs[index];
    for (unsigned i = 0; i < _layout_count; ++i) {
        border(_plots[i].panel, i == index ? kAccent : kLine);
        set_text(_plots[i].title, names[static_cast<unsigned>(_configs[i].kind)]);
    }
    const bool cursor_available = _compass_mode || p.kind != PlotKind::Waterfall;
    const bool cursor_enabled = cursor_available && cursor_config().cursor;
    _cursor_button->setBgColor(lv_color_hex(cursor_enabled ? kAccent : kSoft));
    _cursor_button->label().setTextColor(lv_color_hex(cursor_enabled ? kOnAccent : kText));
    lv_obj_set_state(_cursor_button->get(), LV_STATE_DISABLED, !cursor_available);
    if (_compass_mode) render_rms_cursor();
    if (_type) {
        lv_dropdown_set_selected(_type, static_cast<unsigned>(p.kind));
        const bool spectrum = p.kind == PlotKind::Spectrum;
        const bool waterfall = p.kind == PlotKind::Waterfall;
        for (auto* object : {_span_label, _span})
            lv_obj_update_flag(object, LV_OBJ_FLAG_HIDDEN, spectrum);
        for (auto* object : {_scale_label, _scale})
            lv_obj_update_flag(object, LV_OBJ_FLAG_HIDDEN, waterfall);
        for (auto* object : {_peak_label, _peak_count})
            lv_obj_update_flag(object, LV_OBJ_FLAG_HIDDEN, !spectrum);
        for (auto* object : {_palette_label, _waterfall_palette})
            lv_obj_update_flag(object, LV_OBJ_FLAG_HIDDEN, !waterfall);
        lv_obj_update_flag(_frequency_controls, LV_OBJ_FLAG_HIDDEN, !spectrum && !waterfall);
        lv_dropdown_set_selected(_peak_count, p.spectrum_peak_count);
        lv_dropdown_set_selected(_waterfall_palette, static_cast<unsigned>(p.waterfall_palette));
        lv_dropdown_set_options(_span, waterfall ? "3 s\n5 s\n10 s\n20 s\n30 s" : "100 ms\n200 ms\n1 s\n2 s");
        unsigned selected = 2;
        for (unsigned i = 0; i < kWaveformWindowsMs.size(); ++i)
            if (p.span_ms == kWaveformWindowsMs[i]) selected = i;
        lv_dropdown_set_selected(_span, waterfall ? p.waterfall_window : selected);
        lv_dropdown_set_options(_scale, spectrum ? "自动\n固定\ndB" : "自动\n固定");
        lv_dropdown_set_selected(_scale, spectrum && p.spectrum_db_scale ? 2 : p.auto_scale ? 0 : 1);
        lv_dropdown_set_options(_axes, waterfall ? "跟随全局\nX\nY\n合成矢量" : "跟随全局\nXYZ\nX\nY\n合成矢量");
        const auto mode = static_cast<unsigned>(p.axis_mode);
        lv_dropdown_set_selected(_axes, waterfall ? (mode > 1 ? mode - 1 : 0) : mode);
    }
    for (unsigned i = 0; i < _layout_count; ++i) render_plot(i);
    render_metrics();
}

lv_obj_t* VibescopeView::Impl::trend_canvas(lv_obj_t* parent, int x, int y, int w, int h,
                                          TrendCanvas& cache, bool grid)
{
    cache = {};
    cache.width = w;
    cache.height = h;
    cache.stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565) / 2;
    cache.grid = grid;
    cache.pixels.resize(cache.stride * h);
    auto* canvas = lv_canvas_create(parent);
    position(canvas, x, y, w, h);
    lv_canvas_set_buffer(canvas, cache.pixels.data(), w, h, LV_COLOR_FORMAT_RGB565);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    return canvas;
}

void VibescopeView::Impl::trend(lv_obj_t* target, TrendCanvas& cache, const RmsHistory& values,
                                unsigned channels, unsigned background)
{
    const auto span = kRmsTrendWindowsSeconds[_rms_trend_window];
    if (cache.valid && cache.span == span && cache.channels == channels && cache.values == values) return;
    cache.maximum = .01F;
    cache.has_samples = false;
    for (unsigned channel = 0; channel < values.size(); ++channel) {
        if (!(channels & (1U << channel))) continue;
        const auto& series = values[channel];
        const auto count = std::min<std::size_t>(span + 1, series.size());
        for (auto it = series.end() - count; it != series.end(); ++it) {
            if (*it < 0.F) continue;
            cache.has_samples = true;
            cache.maximum = std::max(cache.maximum, *it * 1.2F);
        }
    }
    bool first = true;
    for (unsigned channel = 0; channel < values.size(); ++channel) {
        if (!(channels & (1U << channel))) continue;
        const auto color = channel == kResultantChannel ? kAccent : kAxes[channel];
        raster_scope_trend(cache.pixels.data(), cache.width, cache.height, cache.stride,
                           values[channel], cache.maximum, lv_color_to_u16(lv_color_hex(background)),
                           lv_color_to_u16(lv_color_hex(color)), first && cache.grid,
                           lv_color_to_u16(lv_color_hex(background == kPanel ? kLine : kGrid)), first, span + 1);
        first = false;
    }
    cache.values = values;
    cache.channels = channels;
    cache.span = span;
    cache.valid = true;
    lv_obj_invalidate(target);
}

void VibescopeView::Impl::render_metrics()
{
    const auto source = _metric_source_index;
    const auto& axes = _analysis[source];
    const auto* frame = resultant_frame(source);
    static const Analysis empty_resultant;
    const auto& combined = frame ? frame->resultant[source] : empty_resultant;
    const bool near_full_scale = _snapshot.near_full_scale[source] || _analysis_near_full_scale[source];
    char value[96];
    const auto now = GetHAL()->millis();
    if (_sidebar_visible && (!_window || _detail_kind == 1) &&
        (_rendered_metric_source != source || _rendered_metric_generation != _snapshot.generation[source] ||
         now - _metrics_draw_tick >= 200)) {
        _metrics_draw_tick = now;
        if (_resultant_display || _rendered_metric_source != source || _rendered_metric_serial != _analysis_serial[source] ||
            _rendered_metric_generation != _snapshot.generation[source]) {
            _rendered_metric_source = source;
            _rendered_metric_serial = _analysis_serial[source];
            _rendered_metric_generation = _snapshot.generation[source];
            auto number = [&](lv_obj_t* label, float v, int digits, bool valid) {
                if (!valid) {
                    set_text(label, "--");
                    return;
                }
                std::snprintf(value, sizeof(value), "%.*f", digits, v);
                set_text(label, value);
            };
            if (_resultant_display) {
                number(_resultant_rms, combined.rms, 3, combined.valid);
                for (unsigned band = 0; band < kRmsBandCount; ++band)
                    number(_resultant_bands[band], combined.band_rms[band], 3, combined.band_valid[band]);
                number(_resultant_details[0], combined.peak, 3, combined.valid);
                number(_resultant_details[1], combined.frequency, 1, combined.frequency > 0);
                number(_resultant_details[2], combined.crest, 2, combined.crest > 0);
                number(_resultant_details[3], combined.kurtosis, 2, combined.crest > 0);
            } else for (unsigned axis = 0; axis < 3; ++axis) {
                const auto& a = axes[axis];
                number(_rms[axis], a.rms, 3, a.valid);
                for (unsigned band = 0; band < kRmsBandCount; ++band)
                    number(_band_rms[band][axis], a.band_rms[band], 3, a.band_valid[band]);
                number(_peak[axis], a.peak, 3, a.valid);
                number(_frequency[axis], a.frequency, 1, a.frequency > 0);
                number(_crest[axis], a.crest, 2, a.valid && a.crest > 0);
                number(_kurtosis[axis], a.kurtosis, 2, a.valid && a.crest > 0);
            }
        }
        const auto range_index = _runtime.accel_range_index(static_cast<Source>(source));
        const auto range_g = source == 0 ? kAdxlRangesG[range_index] : kIcmRangesG[range_index];
        if (axes[0].valid) {
            std::snprintf(value, sizeof(value), "%s | ±%ug | %.2fs | %.0fHz", source_names[source], range_g,
                          axes[0].sample_count / _snapshot.rate[source], _snapshot.rate[source]);
        } else
            std::snprintf(value, sizeof(value), "%s | ±%ug | %.2fs | --Hz",
                          source_names[source], range_g, _metric_window_ms / 1000.F);
        set_text(_metric_window, value);
        set_text_color(_metric_window, near_full_scale ? kAxes[1] : kMuted);
        auto& canvas = _resultant_display ? _resultant_canvas : _trend_canvas;
        auto* scale = _resultant_display ? _resultant_scale : _trend_scale;
        trend(_resultant_display ? _resultant_trend : _trend_chart, canvas, _rms_history[source],
              _resultant_display ? kResultantChannels : kXyzChannels, kPanel);
        if (!canvas.has_samples)
            set_text(scale, "g");
        else {
            std::snprintf(value, sizeof(value), "0-%.3f g", canvas.maximum);
            set_text(scale, value);
        }
    }
    std::snprintf(value, sizeof(value), "-%u s", kRmsTrendWindowsSeconds[_rms_trend_window]);
    for (auto* tick : _sidebar_trend_ticks) set_text(tick, value);
    if (_compass_mode && !_window) render_rms_trend();
}

void VibescopeView::Impl::render_cursor(unsigned index)
{
    auto& plot = _plots[index];
    const auto& config = _configs[index];
    if (!plot.rendered_valid || !plot.rendered.valid || !config.cursor ||
        config.kind == PlotKind::Waterfall || !plot.viewport.count) return;

    const auto source = static_cast<unsigned>(config.source);
    const bool fft = config.kind == PlotKind::Spectrum;
    const unsigned axis = config.all_axes ? config.cursor_axis : config.axis;
    const bool combined = uses_resultant(config);
    const auto* axis_name = combined ? (config.kind == PlotKind::Gyro ? "|w|" : "|a|") : axis_names[axis];
    const auto& analysis = plot_analysis(config, axis);
    const auto& samples = _snapshot.samples[source];
    if (!plot_data_count(config)) return;
    const float step = fft ? analysis.bin_hz : 1000.F / plot.render_rate;
    const bool db = fft && config.spectrum_db_scale;
    const char* xunit = fft ? "Hz" : "ms";
    const char* yunit = db ? "dB" : config.kind == PlotKind::Gyro ? "dps" : "g";
    float position[2]{}, value[2]{};
    for (unsigned marker = 0; marker < (config.dual_cursor ? 2U : 1U); ++marker) {
        const float fraction = marker == 0 ? config.cursor_fraction : config.cursor_b_fraction;
        const auto sample_index = plot.viewport.first + static_cast<std::size_t>(fraction * (plot.viewport.count - 1));
        position[marker] = (sample_index - (fft ? 0.F : plot.trigger_sample_index)) * step;
        float amplitude;
        if (fft) amplitude = analysis.spectrum[std::min(sample_index, analysis.spectrum.size() - 1)];
        else if (combined) {
            const auto& values = resultant_samples(source);
            amplitude = values[std::min(sample_index, values.size() - 1)];
        } else amplitude = samples[std::min(sample_index, samples.size() - 1)][axis] - plot.cursor_means[axis];
        value[marker] = db ? spectrum_db(amplitude) : amplitude;
        const int x = 53 + static_cast<int>((plot.wave_width - 1) * fraction);
        lv_obj_set_x(plot.cursors[marker], x);
        if (config.dual_cursor)
            lv_obj_set_x(plot.cursor_tags[marker], std::clamp(x + (marker ? 4 : -12), 53, 53 + plot.wave_width - 12));
    }
    char label[152];
    if (config.dual_cursor)
        std::snprintf(label, sizeof(label), "%s  A %.1f %s / %.3f %s   B %.1f %s / %.3f %s   %s %.1f %s",
                      axis_name, position[0], xunit, value[0], yunit, position[1], xunit, value[1], yunit,
                      fft ? "dF" : "dT", std::abs(position[1] - position[0]), xunit);
    else
        std::snprintf(label, sizeof(label), "%.1f %s / %s %.3f %s", position[0], xunit,
                      axis_name, value[0], yunit);
    set_text(plot.cursor_value, label);
    plot.rendered.config.cursor_fraction = config.cursor_fraction;
    plot.rendered.config.cursor_b_fraction = config.cursor_b_fraction;
}

void VibescopeView::Impl::render_spectrum_peaks(Plot& plot, const std::array<const Analysis*, 3>& spectra,
                                             unsigned limit, float bin_hz, float minimum, float maximum,
                                             bool db, bool combined)
{
    constexpr int tag_width = 65, tag_height = 17;
    constexpr int left = 53, top = 65;
    const auto& viewport = plot.viewport;
    const auto peaks = spectrum_peaks(spectra, viewport.first, viewport.count, limit);
    std::array<lv_area_t, kMaxSpectrumPeaks> placed{};
    unsigned count = 0;
    for (unsigned i = 0; i < peaks.size(); ++i) {
        const auto& peak = peaks[i];
        auto* dot = plot.peak_dots[i];
        auto* tag = plot.peak_labels[i];
        bool shown = false;
        if (peak.bin && viewport.count > 1 && maximum > minimum && plot.wave_width >= tag_width) {
            const int x = left + static_cast<int>((peak.bin - viewport.first) * (plot.wave_width - 1) /
                                                  (viewport.count - 1));
            const float amplitude = db ? spectrum_db(peak.amplitude) : peak.amplitude;
            const float ratio = std::clamp((amplitude - minimum) / (maximum - minimum), 0.F, 1.F);
            const int y = top + static_cast<int>((1.F - ratio) * (plot.wave_height - 1));
            const lv_point_t offsets[] = {{-tag_width / 2, -tag_height - 4}, {-tag_width / 2, 5},
                                          {5, -tag_height / 2}, {-tag_width - 5, -tag_height / 2}};
            for (const auto& offset : offsets) {
                const auto tx = std::clamp(x + static_cast<int>(offset.x), left, left + plot.wave_width - tag_width);
                const auto ty = y + offset.y;
                if (ty < top || ty + tag_height > top + plot.wave_height) continue;
                const lv_area_t box{tx, ty, tx + tag_width, ty + tag_height};
                const auto overlaps = [&](const lv_area_t& other) {
                    return box.x1 <= other.x2 && box.x2 >= other.x1 && box.y1 <= other.y2 && box.y2 >= other.y1;
                };
                if (std::any_of(placed.begin(), placed.begin() + count, overlaps)) continue;
                const unsigned color = combined ? kAccent : kAxes[peak.axis];
                char label[24];
                std::snprintf(label, sizeof(label), "%.0f Hz", static_cast<double>(peak.bin) * bin_hz);
                set_text(tag, label);
                set_text_color(tag, color);
                lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
                lv_obj_set_pos(dot, x - 2, y - 2);
                lv_obj_set_pos(tag, tx, ty);
                placed[count++] = box;
                shown = true;
                break;
            }
        }
        lv_obj_update_flag(dot, LV_OBJ_FLAG_HIDDEN, !shown);
        lv_obj_update_flag(tag, LV_OBJ_FLAG_HIDDEN, !shown);
    }
}

void VibescopeView::Impl::render_plot(unsigned index)
{
    auto& plot = _plots[index];
    if (_compass_mode || !plot.panel) return;
    const auto& config  = _configs[index];
    const auto source   = static_cast<unsigned>(config.source);
    const auto& samples = _snapshot.samples[source];
    const bool fft = config.kind == PlotKind::Spectrum, waterfall = config.kind == PlotKind::Waterfall,
               gyro           = config.kind == PlotKind::Gyro;
    const bool time_plot = !fft && !waterfall;
    const bool combined = uses_resultant(config);
    const auto* frame = combined ? resultant_frame(source) : nullptr;
    static const std::vector<float> empty_vector;
    const auto& vector_samples = combined && time_plot ? resultant_samples(source) : empty_vector;
    const auto sample_count = combined && time_plot ? vector_samples.size() : samples.size();
    const auto last_sample_us = combined && time_plot && frame ? frame->last_sample_us[source] : _snapshot.last_sample_us[source];
    const float rate = combined && time_plot && frame && !gyro ? frame->rate[source] : _snapshot.rate[source];
    const bool armed = time_plot && _snapshot.armed;
    const auto trigger_time = time_plot && !armed ? _snapshot.trigger_time_us : 0;
    const auto& main_analysis = plot_analysis(config, config.axis);
    const bool valid = _snapshot.valid[source] && sample_count > 8 &&
                       (!(fft || waterfall) || (main_analysis.valid && !main_analysis.spectrum.empty()));
    char label[128];
    const bool near_full_scale = _snapshot.near_full_scale[source] ||
                                (!gyro && _analysis_near_full_scale[std::min(source, 1U)]);
    PlotRenderState state;
    state.config = config;
    state.serial = combined && frame ? (gyro ? frame->gyro_serial : frame->serial[source]) : _snapshot.serial[source];
    state.generation = _snapshot.generation[source];
    state.analysis_serial = _analysis_serial[std::min(source, 1U)];
    state.first = plot.viewport.first;
    state.count = plot.viewport.count;
    state.sample_count = sample_count;
    state.spectrum_count = main_analysis.spectrum.size();
    state.rate = rate;
    state.bin_hz = main_analysis.bin_hz;
    state.axis = config.axis;
    state.valid = valid;
    state.near_full_scale = near_full_scale;
    state.selected = index == _selected;
    state.running = _snapshot.running;
    state.armed = armed;
    state.trigger_time_us = trigger_time;
    state.trigger_position = _snapshot.trigger_position;
    state.resultant = combined;
    if (plot.rendered_valid && plot.rendered == state) return;
    plot.rendered = state;
    plot.rendered_valid = true;
    set_text(plot.source, source_names[source]);
    set_text_color(plot.source, near_full_scale ? kAxes[1] : kMuted);
    for (unsigned axis = 0; axis < 3; ++axis)
        lv_obj_update_flag(plot.legend[axis], LV_OBJ_FLAG_HIDDEN,
                           combined || (!config.all_axes && config.axis != axis));
    set_text(plot.legend[3], gyro ? "|w|" : "|a|");
    lv_obj_update_flag(plot.legend[3], LV_OBJ_FLAG_HIDDEN, !combined);
    lv_obj_set_state(plot.panel, LV_STATE_USER_1, index == _selected);
    lv_obj_update_flag(plot.empty, LV_OBJ_FLAG_HIDDEN, valid);
    lv_obj_update_flag(plot.wave, LV_OBJ_FLAG_HIDDEN, !valid || waterfall);
    lv_obj_update_flag(plot.image, LV_OBJ_FLAG_HIDDEN, !valid || !waterfall);
    if (!valid || !fft)
        for (unsigned i = 0; i < kMaxSpectrumPeaks; ++i) {
            lv_obj_add_flag(plot.peak_dots[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(plot.peak_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    for (unsigned marker = 0; marker < 2; ++marker) {
        const bool hidden = !valid || !config.cursor || waterfall || (marker == 1 && !config.dual_cursor);
        lv_obj_update_flag(plot.cursors[marker], LV_OBJ_FLAG_HIDDEN, hidden);
        lv_obj_update_flag(plot.cursor_tags[marker], LV_OBJ_FLAG_HIDDEN, hidden || !config.dual_cursor);
    }
    lv_obj_update_flag(plot.cursor_value, LV_OBJ_FLAG_HIDDEN, !valid || !config.cursor || waterfall);
    const auto show_trigger = [&](float fraction) {
        const bool visible = fraction >= 0.F && fraction <= 1.F;
        if (visible) {
            const int x = 53 + static_cast<int>(std::lround(fraction * (plot.wave_width - 1)));
            lv_obj_set_x(plot.trigger_line, x);
            lv_obj_set_x(plot.trigger_marker, x - 5);
        }
        lv_obj_update_flag(plot.trigger_line, LV_OBJ_FLAG_HIDDEN, !visible);
        lv_obj_update_flag(plot.trigger_marker, LV_OBJ_FLAG_HIDDEN, !visible);
    };
    const float trigger_fraction = _snapshot.trigger_position / 100.F;
    if (index == _selected)
        for (auto* control : _navigation)
            lv_obj_set_state(control, LV_STATE_DISABLED, _snapshot.running || !valid || waterfall);
    if (!valid) {
        show_trigger(armed ? trigger_fraction : -1.F);
        return;
    }
    auto& viewport = plot.viewport;
    if (fft) {
        const auto band = spectrum_band(main_analysis.spectrum.size(), main_analysis.bin_hz,
                                        config.spectrum_low_hz, config.spectrum_high_hz);
        const bool band_changed = band.first != plot.frequency_band.first || band.count != plot.frequency_band.count ||
                                  main_analysis.bin_hz != plot.spectrum_grid.bin_hz;
        if (band_changed || plot.viewport_kind != config.kind ||
            plot.viewport_source != config.source || _snapshot.running)
            viewport = band;
        plot.frequency_band = band;
        plot.spectrum_grid.bin_hz = main_analysis.bin_hz;
    }
    if (!waterfall) {
        const auto total = fft ? main_analysis.spectrum.size() : sample_count;
        const bool reset = (_snapshot.running && !trigger_time) || !viewport.count || plot.viewport_span != config.span_ms ||
            plot.viewport_source != config.source || plot.viewport_kind != config.kind ||
            plot.viewport_generation != _snapshot.generation[source] || viewport.first + viewport.count > total;
        if (reset && !fft)
            viewport.reset(total, fft ? total : static_cast<std::size_t>(rate * config.span_ms / 1000));
        viewport.clamp(total);
        plot.trigger_sample_index = 0.F;
        if (trigger_time && last_sample_us) {
            const auto delta = static_cast<std::int64_t>(trigger_time) -
                               static_cast<std::int64_t>(last_sample_us);
            plot.trigger_sample_index = static_cast<float>(total - 1) + delta * (rate / 1000000.F);
            if (reset || plot.viewport_trigger_time != trigger_time) {
                const auto start = std::lround(plot.trigger_sample_index - trigger_fraction * (viewport.count - 1));
                viewport.first = static_cast<std::size_t>(std::clamp(start, 0L, static_cast<long>(total - viewport.count)));
            }
        }
        plot.viewport_trigger_time = trigger_time;
        plot.viewport_span = config.span_ms;
        plot.viewport_source = config.source;
        plot.viewport_kind = config.kind;
        plot.viewport_generation = _snapshot.generation[source];
    }
    if (armed) show_trigger(trigger_fraction);
    else if (trigger_time && last_sample_us && viewport.count > 1)
        show_trigger((plot.trigger_sample_index - viewport.first) / (viewport.count - 1));
    else show_trigger(-1.F);
    std::array<float, 3> means{};
    if (time_plot && !gyro && !combined && !samples.empty()) {
        // Stable AC reference across zoom/pan: a partial cycle must not shift the baseline.
        for (const auto& sample : samples)
            for (unsigned a = 0; a < 3; ++a) means[a] += sample[a];
        for (auto& mean : means) mean /= samples.size();
    }
    float maximum = gyro ? 50.F : fft ? .25F : .4F;
    const float bin_hz = main_analysis.bin_hz;
    const float step = fft ? bin_hz : rate > 0 ? 1000.F / rate : 0.F;
    if (waterfall && (!plot.waterfall_ready || plot.generation != _snapshot.generation[source])) {
        const float nyquist = (main_analysis.spectrum.size() - 1) * bin_hz;
        plot.waterfall_low_hz = std::min<float>(config.spectrum_low_hz, std::max(0.F, nyquist - bin_hz));
        plot.waterfall_high_hz = config.spectrum_high_hz
            ? std::min(nyquist, std::max<float>(config.spectrum_high_hz, plot.waterfall_low_hz + bin_hz))
            : nyquist;
    }
    const float x_start = waterfall ? plot.waterfall_low_hz :
        (viewport.first - (fft ? 0.F : plot.trigger_sample_index)) * step;
    const float x_max = waterfall ? plot.waterfall_high_hz - plot.waterfall_low_hz :
        viewport.count > 1 ? (viewport.count - 1) * step : 0.F;
    if (config.auto_scale && !waterfall) {
        maximum = .001F;
        if (combined) {
            const auto& values = fft ? main_analysis.spectrum : vector_samples;
            for (auto i = viewport.first; i < viewport.first + viewport.count; ++i)
                maximum = std::max(maximum, values[i] * 1.15F);
        } else for (unsigned a = 0; a < 3; ++a) {
            if (!config.all_axes && a != config.axis) continue;
            if (fft) {
                const auto& bins = _analysis[source][a].spectrum;
                const auto end = std::min(bins.size(), viewport.first + viewport.count);
                for (auto i = viewport.first; i < end; ++i) maximum = std::max(maximum, bins[i] * 1.15F);
            } else {
                for (std::size_t i = viewport.first; i < viewport.first + viewport.count; ++i)
                    maximum = std::max(maximum, std::abs(samples[i][a] - means[a]) * 1.15F);
            }
        }
    }
    const bool db = fft && config.spectrum_db_scale;
    if (db) maximum = std::ceil(spectrum_db(maximum) / 10.F) * 10.F;
    const float minimum = db ? maximum - 80.F : fft || waterfall || combined ? 0 : -maximum;
    plot.cursor_means = means;
    plot.render_rate = rate;
    render_cursor(index);
    set_text(plot.yunit, waterfall ? "s / dB re 1g" : gyro ? "dps" : fft ? (db ? "dB re 1g" : "g peak") :
             combined ? "|a| / g" : "g AC");
    set_text(plot.xunit, fft || waterfall ? "Hz" : "ms");
    for (unsigned t = 0; t < 5; ++t) {
        const float x = x_start + x_max * t / 4;
        std::snprintf(label, sizeof(label), "%.0f", std::abs(x) < .5F ? 0.F : x);
        set_text(plot.xticks[t], label);
        if (t == 4) lv_obj_set_x(plot.xticks[t], lv_obj_get_width(plot.panel) - 64);
        if (!waterfall) {
            const float y = maximum - (maximum - minimum) * t / 4;
            if (std::abs(y) < .0005F) set_text(plot.yticks[t], "0");
            else {
                std::snprintf(label, sizeof(label), "%.*f", std::abs(y) < 1.F ? 3 : 2, y);
                set_text(plot.yticks[t], label);
            }
        }
    }
    if (waterfall) {
        constexpr auto rows = Plot::kWaterfallRows;
        constexpr auto bins = Plot::kWaterfallBins;
        const auto seconds = kWaterfallWindowsSeconds[config.waterfall_window];
        // Map sample time onto a fixed-duration grid and accumulate peak levels.
        const auto sample_ms = _analysis_sample_time[source];
        const auto bucket = sample_ms * (rows - 1) / (seconds * 1000U) + 1;
        if (!plot.waterfall_ready || plot.generation != _snapshot.generation[source] ||
            bucket < plot.waterfall_bucket) {
            plot.times.fill(0);
            plot.last_serial = 0;
            plot.waterfall_ready = true;
            plot.generation = _snapshot.generation[source];
        }
        const auto previous = plot.waterfall_bucket;
        if (_snapshot.running && plot.last_serial && bucket > previous && bucket - previous <= 3 &&
            sample_ms - plot.waterfall_sample_ms <= 300 && plot.times[previous % rows] == previous) {
            // Hold the last measured spectrum through brief scheduling gaps.
            // Longer interruptions stay visible as blank history.
            for (auto skipped = previous + 1; skipped < bucket; ++skipped) {
                plot.waterfall[skipped % rows] = plot.waterfall[previous % rows];
                plot.times[skipped % rows] = skipped;
            }
        }
        plot.waterfall_bucket = bucket;
        if (plot.last_serial != _analysis_serial[source]) {
            plot.last_serial = _analysis_serial[source];
            plot.waterfall_sample_ms = sample_ms;
            const auto slot = bucket % rows;
            auto& intensity = plot.waterfall[slot];
            if (plot.times[slot] != bucket) intensity.fill(0);
            plot.times[slot] = bucket;
            // Keep columns fixed in Hz even as the probe's estimated sample rate
            // changes slightly from batch to batch.
            const float column_hz = x_max / (bins - 1);
            for (unsigned bin = 0; bin < bins; ++bin) {
                const auto peak = waterfall_column(main_analysis.spectrum, bin_hz,
                                                   x_start + bin * column_hz, column_hz * .5F);
                const float level = std::clamp((20 * std::log10(std::max(peak, .001F)) + 60) / 60, 0.F, 1.F);
                intensity[bin] = std::max(intensity[bin], static_cast<std::uint8_t>(level * 255.F + .5F));
            }
        }
        for (unsigned t = 0; t < 5; ++t) {
            const double seconds_tick = -static_cast<double>(seconds * t) / 4;
            // Negative zero must never show as "-0.0" on the waterfall axis.
            if (std::abs(seconds_tick) < .05) set_text(plot.yticks[t], "0");
            else {
                std::snprintf(label, sizeof(label), "%.1f", seconds_tick);
                set_text(plot.yticks[t], label);
            }
        }
        const auto& palette = waterfall_colors(config.waterfall_palette);
        const auto empty_color = lv_color_to_u16(lv_color_hex(kPlot));
        for (unsigned row = 0; row < rows; ++row) {
            const auto stamp = bucket > row ? bucket - row : 0;
            const auto slot = stamp % rows;
            const bool filled = stamp && plot.times[slot] == stamp;
            const int y0 = row * plot.wave_height / rows, y1 = (row + 1) * plot.wave_height / rows;
            for (unsigned bin = 0; bin < bins; ++bin) {
                const auto color = filled ? palette[plot.waterfall[slot][bin]] : empty_color;
                const int x0 = bin * plot.wave_width / bins, x1 = (bin + 1) * plot.wave_width / bins;
                for (int y = y0; y < y1; ++y)
                    std::fill(plot.wave_pixels.data() + y * plot.wave_stride + x0,
                              plot.wave_pixels.data() + y * plot.wave_stride + x1, color);
            }
        }
        lv_obj_invalidate(plot.image);
        return;
    }
    // Shared opaque grid; plot data are rendered at original sample/bin coordinates.
    for (auto& axis : plot.points) axis.fill(LV_CHART_POINT_NONE);
    std::array<std::uint16_t, 3> colors;
    for (unsigned axis = 0; axis < 3; ++axis) colors[axis] = lv_color_to_u16(lv_color_hex(kAxes[axis]));
    raster_scope_plot(plot.wave_pixels.data(), plot.wave_width, plot.wave_height, plot.wave_stride,
        plot.points, fft, LV_CHART_POINT_NONE, lv_color_to_u16(lv_color_hex(kPlot)),
        lv_color_to_u16(lv_color_hex(kGrid)), colors);
    if (combined) {
        colors[0] = lv_color_to_u16(lv_color_hex(kAccent));
        if (fft)
            raster_scope_spectrum(plot.wave_pixels.data(), plot.wave_width, plot.wave_height, plot.wave_stride,
                &main_analysis, viewport, minimum, maximum, db, false, 0, colors);
        else
            raster_scope_resultant(plot.wave_pixels.data(), plot.wave_width, plot.wave_height, plot.wave_stride,
                vector_samples, viewport, maximum, colors[0]);
    } else if (fft)
        raster_scope_spectrum(plot.wave_pixels.data(), plot.wave_width, plot.wave_height, plot.wave_stride,
            _analysis[source], viewport, minimum, maximum, db, config.all_axes, config.axis, colors);
    else
        raster_scope_time(plot.wave_pixels.data(), plot.wave_width, plot.wave_height, plot.wave_stride,
            samples, viewport, means, maximum, config.all_axes, config.axis, colors);
    if (fft) {
        std::array<const Analysis*, 3> spectra{};
        if (combined) spectra[0] = &main_analysis;
        else for (unsigned axis = 0; axis < 3; ++axis)
            if (config.all_axes || config.axis == axis) spectra[axis] = &_analysis[source][axis];
        render_spectrum_peaks(plot, spectra, config.spectrum_peak_count, bin_hz, minimum, maximum, db, combined);
    }
    lv_obj_invalidate(plot.wave);
}

void VibescopeView::Impl::render_pose()
{
    const bool valid = _snapshot.connected && _snapshot.attitude_valid;
    char value[40];
    for (unsigned i = 0; i < 3; ++i) {
        float angle = _snapshot.attitude[i] - _reference[i];
        if (i == 2) {
            while (angle > 180) angle -= 360;
            while (angle < -180) angle += 360;
        }
        if (valid) {
            std::snprintf(value, sizeof(value), "%+.1f", angle);
            set_text(_angle_labels[i], value);
            _angle_anim[i] = angle;
        } else
            set_text(_angle_labels[i], "--");
    }
    const float pitch = _angle_anim[0].directValue() * .0174533F;
    const float roll  = _angle_anim[1].directValue() * .0174533F;
    const float yaw   = _angle_anim[2].directValue() * .0174533F;
    const Axes drawn{pitch, roll, yaw};
    if (_pose_rendered_initialized && _pose_rendered_valid == valid && _pose_rendered == drawn) return;
    _pose_rendered = drawn;
    _pose_rendered_valid = valid;
    _pose_rendered_initialized = true;
    const PoseProjection project(54, 40, 24, pitch, roll, yaw);
    std::fill(_cube_pixels.begin(), _cube_pixels.end(), lv_color_to_u16(lv_color_hex(kPanel)));
    auto line = [&](lv_point_t from, lv_point_t to, unsigned color) {
        int x = from.x, y = from.y, dx = std::abs(to.x - x), sx = x < to.x ? 1 : -1, dy = -std::abs(to.y - y),
            sy = y < to.y ? 1 : -1, error = dx + dy;
        const auto pixel = lv_color_to_u16(lv_color_hex(valid ? color : kLine));
        for (;;) {
            if (x >= 0 && x < 112 && y >= 0 && y < 100) _cube_pixels[y * 112 + x] = pixel;
            if (x == to.x && y == to.y) break;
            const int twice = 2 * error;
            if (twice >= dy) {
                error += dy;
                x += sx;
            }
            if (twice <= dx) {
                error += dx;
                y += sy;
            }
        }
    };
    for (const auto& edge : kPoseEdges) {
        const auto* a = kPoseVertices[edge[0]];
        const auto* b = kPoseVertices[edge[1]];
        line(project(a[0], a[1], a[2]), project(b[0], b[1], b[2]), kMuted);
    }
    for (unsigned axis = 0; axis < 3; ++axis) {
        set_text_color(_cube_axis_labels[axis], valid ? kAxes[axis] : kLine);
        auto end = project.axis(axis, 1.6F);
        line(project(0, 0, 0), end, kAxes[axis]);
        lv_obj_set_pos(_cube_axis_labels[axis], end.x + 6, end.y);
    }
    lv_obj_invalidate(_cube_canvas);
}

}  // namespace vibescope_view
