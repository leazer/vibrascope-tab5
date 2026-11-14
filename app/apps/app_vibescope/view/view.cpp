/**
 * @file view.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "view_private.h"
#include <map>
#include <chrono>
#include <cstring>
#include <mooncake_log.h>
#if !defined(PLATFORM_BUILD_DESKTOP)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#endif

namespace vibescope_view {
namespace detail {
const char* names[]        = {"时域波形", "幅值频谱", "瀑布图", "Gyro 角速度"};
const char* source_names[] = {"ADXL357", "ICM42688", "ICM42688 / Gyro"};
const char* axis_names[]   = {"X", "Y", "Z"};

void set_text(lv_obj_t* label, const char* value)
{
    // LVGL 9.2 invalidates and reallocates even when text is unchanged.
    // Avoid overflowing its 32 dirty rectangles into a full-screen redraw.
    if (std::strcmp(lv_label_get_text(label), value) != 0) lv_label_set_text(label, value);
}

void set_text_color(lv_obj_t* label, unsigned color)
{
    const auto next = lv_color_hex(color);
    if (lv_color_to_u32(lv_obj_get_style_text_color(label, 0)) != lv_color_to_u32(next))
        lv_obj_set_style_text_color(label, next, 0);
}

lv_obj_t* text(lv_obj_t* parent, const char* value, int x, int y, unsigned color, const lv_font_t* font)
{
    // Numeric labels can later show Chinese status text. Keep their Latin face,
    // but give every requested size a stable Chinese fallback for that transition.
    static std::map<const lv_font_t*, lv_font_t> fonts;
    if (font != &vn_font_16) {
        auto [entry, inserted] = fonts.emplace(font, *font);
        if (inserted) entry->second.fallback = &vn_font_16;
        font = &entry->second;
    }
    auto* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    set_text(label, value);
    lv_obj_set_pos(label, x, y);
    return label;
}
lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h, unsigned color, int radius)
{
    auto* object = lv_obj_create(parent);
    surface(object, color, radius);
    position(object, x, y, w, h);
    return object;
}
void border(lv_obj_t* object, unsigned color)
{
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(color), 0);
}
lv_obj_t* dropdown(lv_obj_t* parent, const char* options, int x, int y, int w, std::function<void(unsigned)> callback)
{
    auto* object = lv_dropdown_create(parent);
    position(object, x, y, w, 44);
    lv_obj_set_style_text_font(object, &vn_font_16, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(kSoft), 0);
    lv_obj_set_style_text_color(object, lv_color_hex(kText), 0);
    lv_obj_set_style_radius(object, 5, 0);
    border(object);
    lv_dropdown_set_options(object, options);
    lv_dropdown_set_dir(object, LV_DIR_TOP);
    lv_dropdown_set_symbol(object, LV_SYMBOL_DOWN);
    auto* list = lv_dropdown_get_list(object);
    lv_obj_set_style_text_font(list, &vn_font_16, 0);
    lv_obj_set_style_text_color(list, lv_color_hex(kText), 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(kPanel), 0);
    lv_obj_set_style_border_color(list, lv_color_hex(kLine), 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(kSelected), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_color(list, lv_color_hex(kAccent), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(list, lv_color_hex(kSelected), static_cast<lv_style_selector_t>(LV_PART_SELECTED) | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(list, lv_color_hex(kAccent), static_cast<lv_style_selector_t>(LV_PART_SELECTED) | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(list, lv_color_hex(kSoft), static_cast<lv_style_selector_t>(LV_PART_SELECTED) | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(list, lv_color_hex(kText), static_cast<lv_style_selector_t>(LV_PART_SELECTED) | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(object, lv_color_hex(kAccent), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(object, lv_color_hex(kAccent), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(object, lv_color_hex(kAccent), LV_STATE_EDITED);
    lv_obj_set_style_pad_ver(list, 12, LV_PART_SELECTED);
    auto* cb = new std::function<void(unsigned)>(std::move(callback));
    lv_obj_add_event_cb(
        object,
        [](lv_event_t* e) {
            auto* fn = static_cast<std::function<void(unsigned)>*>(lv_event_get_user_data(e));
            (*fn)(lv_dropdown_get_selected(lv_event_get_target_obj(e)));
        },
        LV_EVENT_VALUE_CHANGED, cb);
    lv_obj_add_event_cb(
        object, [](lv_event_t* e) { delete static_cast<std::function<void(unsigned)>*>(lv_event_get_user_data(e)); },
        LV_EVENT_DELETE, cb);
    return object;
}
lv_obj_t* chart(lv_obj_t* parent, int x, int y, int w, int h)
{
    auto* c = lv_chart_create(parent);
    position(c, x, y, w, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(kPlot), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 0, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(kLine), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_line_color(c, lv_color_hex(kLine), 0);
    lv_obj_set_style_line_width(c, 1, LV_PART_ITEMS);
    lv_obj_set_style_size(c, 0, 0, LV_PART_INDICATOR);
    lv_chart_set_type(c, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(c, 256);
    lv_chart_set_div_line_count(c, 5, 5);
    lv_obj_remove_flag(c, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return c;
}

}  // namespace detail

void VibescopeView::Impl::display_event(lv_event_t* event)
{
    auto* self      = static_cast<Impl*>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_REFR_READY && self->_preview_draw_pending) {
        self->_preview_draw_pending = false;
        std::printf("vibescope-screen: preview draw_ms=%u\n",
                    static_cast<unsigned>(GetHAL()->millis() - self->_preview_draw_started));
    }
    if (code != LV_EVENT_RENDER_START && code != LV_EVENT_RENDER_READY && code != LV_EVENT_FLUSH_START &&
        code != LV_EVENT_FLUSH_FINISH && code != LV_EVENT_FLUSH_WAIT_START && code != LV_EVENT_FLUSH_WAIT_FINISH)
        return;
    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
    if (code == LV_EVENT_FLUSH_WAIT_START)
        self->_wait_started = now;
    else if (code == LV_EVENT_FLUSH_WAIT_FINISH) {
        const auto elapsed = now - self->_wait_started;
        self->_wait_sum += elapsed;
        self->_wait_max = std::max(self->_wait_max, elapsed);
    } else if (code == LV_EVENT_FLUSH_START) {
        ++self->_render_flushes;
        const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(event));
        if (area) self->_flush_pixels += static_cast<std::uint64_t>(lv_area_get_width(area)) * lv_area_get_height(area);
        self->_flush_started = now;
    } else if (code == LV_EVENT_FLUSH_FINISH) {
        const auto elapsed = now - self->_flush_started;
        self->_flush_sum += elapsed;
        self->_flush_max = std::max(self->_flush_max, elapsed);
    } else if (code == LV_EVENT_RENDER_START)
        self->_render_started = now;
    else if (self->_render_started) {
        const auto elapsed = now - self->_render_started;
        self->_render_sum += elapsed;
        self->_render_max = std::max(self->_render_max, elapsed);
        ++self->_render_frames;
        self->_render_started = 0;
    }
}

void VibescopeView::Impl::init()
{
    LvglLockGuard lock;
    _previous = lv_screen_active();
    _screen   = std::make_unique<Container>(nullptr);
    surface(_screen->get(), kBackground);
    _screen->setSize(kWidth, kHeight);
    init_ppa_draw();
    _last_gpu = ppa_stats();
    lv_display_add_event_cb(lv_display_get_default(), display_event, LV_EVENT_ALL, this);
    _configs[1].kind = PlotKind::Spectrum;
    _configs[2].kind = PlotKind::Waterfall;
    for (auto& anim : _angle_anim) {
        anim.easingOptions().duration       = .15;
        anim.easingOptions().easingFunction = ease::linear;
        anim.teleport(0);
        anim.play();
    }
    build_sidebar();
    build_status();
    build_footer();
    layout_plots();
    _screenshot_flash = panel(_screen->get(), 0, 0, kWidth, kHeight, 0xffffff);
    lv_obj_set_style_bg_opa(_screenshot_flash, LV_OPA_50, 0);
    lv_obj_remove_flag(_screenshot_flash, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(_screenshot_flash, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(_screen->get());
    _device_preferences = GetHAL()->getDevicePreferences();
    lv_display_trigger_activity(lv_obj_get_display(_screen->get()));
    _analysis_worker.start();
    _last_preferences = current_preferences().values();
    _runtime.start();
    _updated = GetHAL()->millis() - 1000;
}

ScopePreferences VibescopeView::Impl::current_preferences() const
{
    return {_metric_source_index, _runtime.accel_range_index(Source::Adxl), _runtime.accel_range_index(Source::Icm),
            _metric_window_ms, static_cast<unsigned>(_trigger_threshold), _record_duration_index, _layout_count,
            _sidebar_visible, _trigger_position, _resultant_display, _configs, _rms_trend_window};
}

bool VibescopeView::Impl::sync_preferences()
{
    const auto current = current_preferences();
    const auto values = current.values();
    const auto loaded = _runtime.take_loaded_preferences();
    if (values != _last_preferences) {
        _last_preferences = values;
        _preferences_edited = true;
        _runtime.save_preferences(current);
    } else if (loaded && !_preferences_edited) {
        _last_preferences = loaded->values();
        _metric_source_index = loaded->source;
        _runtime.set_accel_range(Source::Adxl, loaded->adxl_range);
        _runtime.set_accel_range(Source::Icm, loaded->icm_range);
        _metric_window_ms = loaded->statistics_ms;
        _trigger_threshold = static_cast<int>(loaded->trigger_centi_g);
        _trigger_position = loaded->trigger_position;
        _record_duration_index = loaded->duration_index;
        _runtime.set_record_duration(kRecordDurationsSeconds[_record_duration_index]);
        _configs = loaded->plots;
        _rms_trend_window = loaded->rms_trend_window;
        set_resultant_display(loaded->resultant_display);
        _layout_count = loaded->layout;
        _selected = std::min(_selected, _layout_count - 1);
        lv_dropdown_set_selected(_layout_dropdown, _layout_count - 1);
        _sidebar_visible = loaded->sidebar_visible;
        return true;
    }
    return false;
}

void VibescopeView::Impl::set_acquisition_running(bool run)
{
    _hold_waveforms = !run;
    _runtime.set_running(run);
    if (!run) {
        _snapshot.running = _snapshot.armed = _snapshot.temperature_live = false;
        set_text(_status, "STOP");
        set_text(_run->label().get(), "运行");
    }
}

void VibescopeView::Impl::update()
{
    if (_shutdown_pending) service_shutdown();
    LvglLockGuard lock;
    const auto now = GetHAL()->millis();
    if (_device_preferences_dirty && now - _device_preferences_changed >= 500) {
        if (!save_device_preferences()) _device_preferences_changed = now;
    }
    update_keyboard(now);
    if (!_shutdown_pending) service_idle_power();
    if (!lv_obj_has_flag(_screenshot_flash, LV_OBJ_FLAG_HIDDEN) && now - _screenshot_flash_at >= 100)
        lv_obj_add_flag(_screenshot_flash, LV_OBJ_FLAG_HIDDEN);
    if (lv_obj_has_state(_screenshot_button->get(), LV_STATE_CHECKED)) {
        const auto result = _runtime.take_screenshot_result();
        if (!result.empty()) {
            lv_obj_remove_state(_screenshot_button->get(), LV_STATE_CHECKED);
            if (result == "截屏已保存") {
                _screenshot_flash_at = now;
                lv_obj_move_foreground(_screenshot_flash);
                lv_obj_remove_flag(_screenshot_flash, LV_OBJ_FLAG_HIDDEN);
            } else {
                ui::pop_a_toast(result, ui::toast_type::error, 2000, &vn_font_16, 0);
            }
        }
    }
    if (auto* request = _file_manager_request) {
        _file_manager_request = nullptr;
        if (request == _record->get()) show_record_config();
        else show_screenshot_files();
    }
    if (_window) {
        _window->update();
        if (_window->getState() == ui::Window::Closed) close_window();
    }
    if (_cal_step && now - _cal_updated >= 80) {
        _cal_updated = now;
        update_calibration();
    }
    if (now - _status_icon_tick >= 1000) {
        _status_icon_tick = now;
        update_status_icons();
        if (_frequency_high) update_frequency_limit();
    }
    for (auto& anim : _angle_anim) anim.update();
    if (!_pose_flash.done()) {
        _pose_flash.update();
        lv_obj_set_style_bg_opa(_pose_hit, static_cast<lv_opa_t>(_pose_flash.directValue()), 0);
    }
    if (!_window && (_sidebar_visible || _compass_mode) && now - _pose_draw_tick >= 33 &&
        std::any_of(_angle_anim.begin(), _angle_anim.end(), [](auto& anim) { return !anim.done(); })) {
        _pose_draw_tick = now;
        render_pose();
        if (_compass_mode) render_compass();
    }
    // Acquisition runs independently. Request fresh plots at 20 Hz while
    // measuring; the display task naturally limits delivery to its capacity.
    if (now - _updated < (_snapshot.connected ? 50U : 200U)) return;
    _updated = now;
    if (sync_preferences()) layout_sidebar();
    update_frame(now);
}

// Analysis requests contain vectors and metadata. Retire those locals
// before calling the UI's floating-point formatters on the TAB5 main task.
#if defined(ESP_PLATFORM)
[[gnu::noinline]]
#endif
void VibescopeView::Impl::update_analysis(std::uint32_t now)
{
    std::array<bool, 2> resultant_metrics{};
    std::array<bool, 3> resultant_time{};
    resultant_metrics[_metric_source_index] = _resultant_display;
    for (unsigned i = 0; i < _layout_count; ++i) {
        const auto& config = _configs[i];
        if (!uses_resultant(config)) continue;
        const auto source = static_cast<unsigned>(config.source);
        if (config.kind == PlotKind::Time || config.kind == PlotKind::Gyro) resultant_time[source] = true;
        else resultant_metrics[source] = true;
    }
    // Spectrum/waterfall bands raise the FFT window so the drawn band stays
    // dense; the widest demand per source wins. A full-range band naturally
    // rounds back to the default 2048-point window.
    std::array<std::size_t, 2> spectrum_bins{};
    for (unsigned i = 0; i < _layout_count; ++i) {
        const auto& config = _configs[i];
        if (config.kind != PlotKind::Spectrum && config.kind != PlotKind::Waterfall) continue;
        const auto source = static_cast<unsigned>(config.source);
        const float rate = _snapshot.rate[source];
        const float high = config.spectrum_high_hz ? static_cast<float>(config.spectrum_high_hz) : rate * .5F;
        const float width = high - static_cast<float>(config.spectrum_low_hz);
        if (rate <= 0 || width < 1.F) continue;
        spectrum_bins[source] = std::max(spectrum_bins[source], static_cast<std::size_t>(512.F * rate / width));
    }
    for (auto& bins : spectrum_bins) {
        std::size_t rounded = 1;
        while (rounded < bins) rounded *= 2;
        bins = std::clamp(rounded, kScopeFftLimit, kScopeFftZoomLimit);
    }
    bool changed = !_analysis_submitted || _submitted_window_ms != _metric_window_ms ||
        _submitted_resultant_metrics != resultant_metrics || _submitted_resultant_time != resultant_time ||
        _submitted_spectrum_bins != spectrum_bins ||
        (resultant_time[2] && _submitted_gyro_serial != _snapshot.serial[2]);
    for (unsigned source = 0; source < 2; ++source)
        changed |= _submitted_serial[source] != _snapshot.serial[source] ||
                   _submitted_generation[source] != _snapshot.generation[source] ||
                   _submitted_rate[source] != _snapshot.rate[source] ||
                   _submitted_valid[source] != _snapshot.valid[source];
    if (changed) {
        ScopeAnalysisInput input;
        input.window_ms = _submitted_window_ms = _metric_window_ms;
        input.resultant_metrics = _submitted_resultant_metrics = resultant_metrics;
        input.resultant_time = _submitted_resultant_time = resultant_time;
        input.spectrum_bins = _submitted_spectrum_bins = spectrum_bins;
        input.last_sample_us = _snapshot.last_sample_us;
        if (resultant_time[2] && _snapshot.valid[2]) {
            input.gyro = _snapshot.samples[2];
            input.gyro_serial = _submitted_gyro_serial = _snapshot.serial[2];
            input.gyro_generation = _snapshot.generation[2];
        }
        for (unsigned source = 0; source < 2; ++source) {
            input.valid[source] = _submitted_valid[source] = _snapshot.valid[source];
            input.near_full_scale[source] = _snapshot.near_full_scale[source];
            input.rate[source] = _submitted_rate[source] = _snapshot.rate[source];
            input.serial[source] = _submitted_serial[source] = _snapshot.serial[source];
            input.generation[source] = _submitted_generation[source] = _snapshot.generation[source];
            input.sample_time_ms[source]                             = _snapshot.updated_ms[source];
            if (input.valid[source]) {
                const auto& samples = _snapshot.samples[source];
                input.stats_count[source] = static_cast<std::size_t>(_snapshot.rate[source] * _metric_window_ms / 1000);
                const auto count = resultant_time[source] ? samples.size() :
                    std::min(samples.size(), std::max({kScopeFftLimit, spectrum_bins[source], input.stats_count[source]}));
                input.samples[source].assign(samples.end() - count, samples.end());
            }
        }
        _analysis_worker.submit(std::move(input));
        _analysis_submitted = true;
    }
    const auto result = _analysis_worker.latest();
    if (result) _analysis_cost_us = result->cost_us;
    if (!resultant_time[2] || !_snapshot.valid[2]) _gyro_result.reset();
    else if (result && result->resultant_time[2] && result->gyro_generation == _snapshot.generation[2] &&
             result->gyro_serial <= _snapshot.serial[2] && !result->resultant_samples[2].empty()) _gyro_result = result;
    for (unsigned source = 0; source < 2; ++source) {
        const auto& accepted = _accepted_results[source];
        const auto matches = [&](const auto& output) {
            return output && output->window_ms == _metric_window_ms &&
                output->resultant_metrics[source] == resultant_metrics[source] &&
                output->resultant_time[source] == resultant_time[source] &&
                output->spectrum_bins[source] == spectrum_bins[source];
        };
        const bool matching_window = matches(result);
        if (_hold_waveforms && (!matching_window || result->serial[source] != _snapshot.serial[source] ||
                                matches(accepted))) continue;
        if (matching_window && result->usable(source, _snapshot.valid[source], _snapshot.generation[source],
                                     _snapshot.serial[source], _snapshot.rate[source])) {
            if (accepted != result) {
                if (accepted && accepted->window_ms != result->window_ms) {
                    for (auto& series : _rms_history[source]) series.clear();
                    _trend_serial[source] = 0;
                    _rendered_metric_source = 2;
                }
                _analysis[source] = result->axes[source];
                _accepted_results[source] = result;
            }
            _analysis_near_full_scale[source] = result->near_full_scale[source];
            _analysis_serial[source]      = result->serial[source];
            _analysis_sample_time[source] = result->sample_time_ms[source];
        } else if (!accepted || !accepted->usable(source, _snapshot.valid[source], _snapshot.generation[source],
                                                  _snapshot.serial[source], _snapshot.rate[source])) {
            _accepted_results[source].reset();
            if (_analysis_serial[source]) _analysis[source] = {};
            _analysis_near_full_scale[source] = false;
            _analysis_serial[source] = 0;
        }
        if (!_snapshot.valid[source])
            for (auto& series : _rms_history[source]) series.clear();
    }
    for (unsigned source = 0; source < 2; ++source)
        if (_trend_generation[source] != _snapshot.generation[source]) {
            _trend_generation[source] = _snapshot.generation[source];
            for (auto& series : _rms_history[source]) series.clear();
        }
    const auto elapsed = (now - _trend_tick) / 1000;
    if (elapsed == 0) return;
    _trend_tick += elapsed * 1000;
    constexpr auto capacity = kRmsTrendWindowsSeconds.back() + 1;
    for (unsigned source = 0; source < 2; ++source) {
        if (!_snapshot.valid[source] || (!_snapshot.running && !_snapshot.armed)) continue;
        auto& history = _rms_history[source];
        const auto count = std::min<unsigned>(elapsed, capacity);
        for (auto& series : history) {
            if (series.size() + count > capacity)
                series.erase(series.begin(), series.begin() + (series.size() + count - capacity));
            series.insert(series.end(), count, -1.F);
        }
        if (_trend_serial[source] == _analysis_serial[source]) continue;
        _trend_serial[source] = _analysis_serial[source];
        const auto& axes = _analysis[source];
        for (unsigned axis = 0; axis < axes.size(); ++axis)
            if (axes[axis].valid) history[axis].back() = axes[axis].rms;
        if (std::all_of(axes.begin(), axes.end(), [](const auto& axis) { return axis.valid; })) {
            // The RMS of the AC vector is the root of the sum of the three mean squares.
            history[kResultantChannel].back() = std::hypot(axes[0].rms, axes[1].rms, axes[2].rms);
        }
    }
}

// ScopeSnapshot temporaries hold the metadata for all streams. Keep them off
// the rendering stack, especially when formatting waveform tick labels.
#if defined(ESP_PLATFORM)
[[gnu::noinline]]
#endif
void VibescopeView::Impl::update_snapshot()
{
    auto& next = _next_snapshot;
    next = _runtime.snapshot(false);
    if (_hold_waveforms) {
        // Keep the displayed sample/analysis frame while USB drains the stop boundary.
        // The live metadata (link, pose and recorder) still comes from the runtime.
        next.serial = _snapshot.serial;
        next.generation = _snapshot.generation;
        next.valid = _snapshot.valid;
        next.rate = _snapshot.rate;
        next.near_full_scale = _snapshot.near_full_scale;
        next.updated_ms = _snapshot.updated_ms;
        next.last_sample_us = _snapshot.last_sample_us;
        next.samples = std::move(_snapshot.samples);
        next.running = next.armed = next.temperature_live = false;
    } else if (next.serial != _snapshot.serial || next.generation != _snapshot.generation ||
        next.valid != _snapshot.valid || next.rate != _snapshot.rate)
        next = _runtime.snapshot();
    else
        next.samples = std::move(_snapshot.samples);
    _snapshot = std::move(next);
}

#if defined(ESP_PLATFORM)
[[gnu::noinline]]
#endif
void VibescopeView::Impl::update_frame(std::uint32_t now)
{
    const auto started = std::chrono::steady_clock::now();
    update_snapshot();
    const auto copied  = std::chrono::steady_clock::now();
    _record_intent     = _snapshot.record_requested;
    update_analysis(now);
    const auto analyzed = std::chrono::steady_clock::now();
    set_text(_status, _snapshot.armed ? "TRIGGER" : _snapshot.running ? "RUN" : "STOP");
    lv_obj_set_state(_trigger_button->get(), LV_STATE_CHECKED, _snapshot.armed);
    set_text_color(_trigger_button->label().get(), _snapshot.armed ? kOnAccent : kText);
    set_text(_run->label().get(), _snapshot.running || _snapshot.armed ? "停止" : "运行");
    set_text(_record->label().get(), _snapshot.recording ? "结束录制" : "录制");
    const auto record_color = lv_color_hex(_snapshot.recording ? 0x663536 : kSoft);
    if (lv_color_to_u32(lv_obj_get_style_bg_color(_record->get(), 0)) != lv_color_to_u32(record_color))
        _record->setBgColor(record_color);
    char label[128];
    if (_snapshot.temperature_valid) {
        std::snprintf(label, sizeof(label), "TEMP %.1f℃%s", _snapshot.temperature,
                      _snapshot.temperature_live ? "" : "*");
        set_text(_temperature, label);
    } else
        set_text(_temperature, "TEMP -- ℃");
    if (_snapshot.recording) {
        std::snprintf(label, sizeof(label), "REC %02u:%02u", static_cast<unsigned>(_snapshot.record_seconds / 60),
                      static_cast<unsigned>(_snapshot.record_seconds % 60));
        set_text(_record_status, label);
    } else
        set_text(_record_status, "REC");
    constexpr unsigned recording_red = 0xff4d4f;
    set_text_color(_record_status, _snapshot.recording ? recording_red : kMuted);
    const auto dot_color = lv_color_hex(_snapshot.recording ? recording_red : kMuted);
    if (lv_color_to_u32(lv_obj_get_style_bg_color(_record_dot, 0)) != lv_color_to_u32(dot_color))
        lv_obj_set_style_bg_color(_record_dot, dot_color, 0);
    set_text(_probe_status, _snapshot.connected ? "已连接" : "未连接");
    set_text_color(_probe_status, _snapshot.connected ? kAccent : 0x686868);
    const auto time   = std::time(nullptr);
    const auto* local = std::localtime(&time);
    if (local) {
        std::strftime(label, sizeof(label), "%Y-%m-%d %H:%M:%S", local);
        set_text(_clock, label);
    }
    if (_snapshot.message != _last_message) {
        if (_snapshot.message.find("失败") != std::string::npos) {
            _record_intent = false;
            ui::pop_a_toast(_snapshot.message, ui::toast_type::error, 3500, &vn_font_16, 0);
        }
        _last_message = _snapshot.message;
    }
    if (_detail_kind == 5 || _detail_kind == 9) update_file_list();
    if (_detail_kind == 8) update_device_export();
    auto section_started = std::chrono::steady_clock::now();
    render_metrics();
    auto section_finished = std::chrono::steady_clock::now();
    _section_us[0] += std::chrono::duration_cast<std::chrono::microseconds>(section_finished - section_started).count();
    section_started = section_finished;
    if (!_window && (_sidebar_visible || _compass_mode)) {
        render_pose();
        if (_compass_mode) render_compass();
    }
    section_finished = std::chrono::steady_clock::now();
    _section_us[1] += std::chrono::duration_cast<std::chrono::microseconds>(section_finished - section_started).count();
    for (unsigned i = 0; !_window && !_compass_mode && i < _layout_count; ++i) {
        section_started = std::chrono::steady_clock::now();
        render_plot(i);
        _section_us[i + 2] += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - section_started).count();
    }
    // Low-rate diagnostics distinguish analysis/lock contention from rendering.
    const auto finished = std::chrono::steady_clock::now();
    const auto us       = [](auto duration) {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
    };
    _perf_copy += us(copied - started);
    _perf_analysis += us(analyzed - copied);
    _perf_apply += us(finished - analyzed);
    _perf_max = std::max(_perf_max, us(finished - started));
    ++_perf_updates;
    if (now - _perf_tick >= 5000) {
        // fmt's floating-point formatter exceeds the board's 3584-byte main
        // stack. Keep instrumentation integer-only, including its output path.
        unsigned stack_free = 0;
#if !defined(PLATFORM_BUILD_DESKTOP)
        stack_free = uxTaskGetStackHighWaterMark(nullptr);
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER && !CONFIG_FREERTOS_SMP
        static std::uint64_t previous_us = 0;
        static std::uint32_t previous_idle[2]{};
        const auto now_us = static_cast<std::uint64_t>(esp_timer_get_time());
        const std::uint32_t idle[2] = {ulTaskGetIdleRunTimeCounterForCore(0), ulTaskGetIdleRunTimeCounterForCore(1)};
        if (previous_us && now_us > previous_us) {
            const auto elapsed = now_us - previous_us;
            std::printf("vibescope-cpu: run=%u record=%u idle0_pct=%u idle1_pct=%u record_drops=%u queue_peak=%u\n",
                        unsigned(_snapshot.running), unsigned(_snapshot.recording),
                        unsigned(std::min<std::uint64_t>(100, 100ULL * std::uint32_t(idle[0] - previous_idle[0]) / elapsed)),
                        unsigned(std::min<std::uint64_t>(100, 100ULL * std::uint32_t(idle[1] - previous_idle[1]) / elapsed)),
                        unsigned(_snapshot.record_drops), _snapshot.record_queue_peak);
        }
        previous_us = now_us;
        previous_idle[0] = idle[0];
        previous_idle[1] = idle[1];
#endif
#endif
        std::printf(
            "vibescope-perf: updates=%u copy_us=%llu analysis_us=%llu apply_us=%llu max_us=%llu idle=%u stack=%u "
            "samples=%u/%u/%u worker_us=%llu\n",
            static_cast<unsigned>(_perf_updates), static_cast<unsigned long long>(_perf_copy / _perf_updates),
            static_cast<unsigned long long>(_perf_analysis / _perf_updates),
            static_cast<unsigned long long>(_perf_apply / _perf_updates), static_cast<unsigned long long>(_perf_max),
            static_cast<unsigned>(lv_timer_get_idle()), stack_free, static_cast<unsigned>(_snapshot.samples[0].size()),
            static_cast<unsigned>(_snapshot.samples[1].size()), static_cast<unsigned>(_snapshot.samples[2].size()),
            static_cast<unsigned long long>(_analysis_cost_us));
        std::printf(
            "vibescope-link: connected=%u run=%u batches=%u/%u rejected=%u/%u last_quality=%02x/%02x crc=%u gaps=%u "
            "drops=%u\n",
            static_cast<unsigned>(_snapshot.connected), static_cast<unsigned>(_snapshot.running),
            static_cast<unsigned>(_snapshot.batches[0]), static_cast<unsigned>(_snapshot.batches[1]),
            static_cast<unsigned>(_snapshot.rejected_batches[0]), static_cast<unsigned>(_snapshot.rejected_batches[1]),
            static_cast<unsigned>(_snapshot.last_rejected_quality[0]),
            static_cast<unsigned>(_snapshot.last_rejected_quality[1]), static_cast<unsigned>(_snapshot.crc_errors),
            static_cast<unsigned>(_snapshot.gaps), static_cast<unsigned>(_snapshot.queue_drops));
        std::printf(
            "vibescope-pump: ingest_max_us=%u attitude_max_us=%u stack=%u rxhw=%u overruns=%u timeouts=%u retries=%u\n",
            static_cast<unsigned>(_snapshot.ingest_max_us), static_cast<unsigned>(_snapshot.attitude_max_us),
            static_cast<unsigned>(_snapshot.worker_stack), static_cast<unsigned>(_snapshot.rx_high_water),
            static_cast<unsigned>(_snapshot.rx_overruns), static_cast<unsigned>(_snapshot.timeouts),
            static_cast<unsigned>(_snapshot.retries));
        std::printf("vibescope-range: batches=%u/%u window=%u/%u/%u\n",
                    static_cast<unsigned>(_snapshot.near_full_scale_batches[0]),
                    static_cast<unsigned>(_snapshot.near_full_scale_batches[1]),
                    static_cast<unsigned>(_snapshot.near_full_scale[0]),
                    static_cast<unsigned>(_snapshot.near_full_scale[1]),
                    static_cast<unsigned>(_snapshot.near_full_scale[2]));
        std::printf("vibescope-pose: valid=%u pitch_mdeg=%d roll_mdeg=%d yaw_mdeg=%d\n",
                    static_cast<unsigned>(_snapshot.attitude_valid),
                    static_cast<int>(_snapshot.attitude[0] * 1000.F),
                    static_cast<int>(_snapshot.attitude[1] * 1000.F),
                    static_cast<int>(_snapshot.attitude[2] * 1000.F));
        std::printf("vibescope-temperature: source=ADXL357 valid=%u live=%u c_x10=%d\n",
                    static_cast<unsigned>(_snapshot.temperature_valid),
                    static_cast<unsigned>(_snapshot.temperature_live),
                    static_cast<int>(_snapshot.temperature * 10.F));
        std::printf(
            "vibescope-render: frames=%u avg_us=%llu max_us=%llu flushes=%u flush_us=%llu flush_max=%llu wait_us=%llu "
            "wait_max=%llu\n",
            _render_frames, static_cast<unsigned long long>(_render_frames ? _render_sum / _render_frames : 0),
            static_cast<unsigned long long>(_render_max), _render_flushes, static_cast<unsigned long long>(_flush_sum),
            static_cast<unsigned long long>(_flush_max), static_cast<unsigned long long>(_wait_sum),
            static_cast<unsigned long long>(_wait_max));
        std::printf("vibescope-sections: metric_us=%llu pose_us=%llu plot_us=%llu/%llu/%llu flush_px=%llu\n",
                    static_cast<unsigned long long>(_section_us[0] / _perf_updates),
                    static_cast<unsigned long long>(_section_us[1] / _perf_updates),
                    static_cast<unsigned long long>(_section_us[2] / _perf_updates),
                    static_cast<unsigned long long>(_section_us[3] / _perf_updates),
                    static_cast<unsigned long long>(_section_us[4] / _perf_updates),
                    static_cast<unsigned long long>(_flush_pixels));
        for (auto& section : _section_us) section = 0;
        _flush_pixels = 0;
        const auto& gpu = ppa_stats();
        const auto frame_count = std::max(1U, _render_frames);
        std::printf("vibescope-gpu-frame: image_us=%llu fill_us=%llu cache_us=%llu\n",
                    static_cast<unsigned long long>((gpu.image_us - _last_gpu.image_us) / frame_count),
                    static_cast<unsigned long long>((gpu.fill_us - _last_gpu.fill_us) / frame_count),
                    static_cast<unsigned long long>((gpu.cache_us - _last_gpu.cache_us) / frame_count));
        for (unsigned type = 1; type < 14; ++type)
            if (gpu.sw_tasks[type] != _last_gpu.sw_tasks[type])
                std::printf("vibescope-sw-frame: type=%u tasks=%llu us=%llu\n", type,
                            static_cast<unsigned long long>(gpu.sw_tasks[type] - _last_gpu.sw_tasks[type]),
                            static_cast<unsigned long long>((gpu.sw_us[type] - _last_gpu.sw_us[type]) / frame_count));
        _last_gpu = gpu;
        std::printf("vibescope-dma: image_tasks=%llu\n", static_cast<unsigned long long>(gpu.dma_image_tasks));
        _render_frames  = 0;
        _render_flushes = 0;
        _render_sum = _render_max = 0;
        _flush_sum = _flush_max = 0;
        _wait_sum = _wait_max = 0;
        std::printf("vibescope-ui: layout=%u plots=%u/%u/%u modal=%d\n", _layout_count,
                    static_cast<unsigned>(_configs[0].kind), static_cast<unsigned>(_configs[1].kind),
                    static_cast<unsigned>(_configs[2].kind), _window ? _detail_kind : 0);
        std::printf(
            "vibescope-ppa: active=%u align=%u images=%llu fills=%llu pixels=%llu gpu_us=%llu max_us=%llu "
            "fallback=%llu rejected=%llu errors=%llu\n",
            static_cast<unsigned>(gpu.initialized), static_cast<unsigned>(gpu.alignment),
            static_cast<unsigned long long>(gpu.image_tasks), static_cast<unsigned long long>(gpu.fill_tasks),
            static_cast<unsigned long long>(gpu.pixels), static_cast<unsigned long long>(gpu.elapsed_us),
            static_cast<unsigned long long>(gpu.max_us), static_cast<unsigned long long>(gpu.fallbacks),
            static_cast<unsigned long long>(gpu.rejected), static_cast<unsigned long long>(gpu.errors));
        _perf_tick    = now;
        _perf_updates = 0;
        _perf_copy = _perf_analysis = _perf_apply = _perf_max = 0;
    }
}

VibescopeView::VibescopeView() : _impl(std::make_unique<Impl>())
{
}
VibescopeView::~VibescopeView() = default;
void VibescopeView::init()
{
    _impl->init();
}
void VibescopeView::update()
{
    _impl->update();
}
}  // namespace vibescope_view
