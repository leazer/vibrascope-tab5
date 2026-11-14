/**
 * @file view_private.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include "view.h"
#include "style.h"
#include "../scope_runtime.h"
#include "../scope_analysis_worker.h"
#include "../scope_raster.h"
#include "../scope_ppa.h"
#include "../scope_render_state.h"
#include <hal/hal.h>
#include <smooth_lvgl.h>
#include <smooth_ui_toolkit.h>
#include <apps/utils/ui/window.h>
#include <apps/utils/ui/toast.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace vibescope;

namespace vibescope_view {
namespace detail {
extern const char* names[];
extern const char* source_names[];
extern const char* axis_names[];
void set_text(lv_obj_t*, const char*);
void set_text_color(lv_obj_t*, unsigned);
lv_obj_t* text(lv_obj_t*, const char*, int, int, unsigned color = kText, const lv_font_t* font = &vn_font_16);
lv_obj_t* panel(lv_obj_t*, int, int, int, int, unsigned color = kPanel, int radius = 0);
void border(lv_obj_t*, unsigned color = kLine);
lv_obj_t* dropdown(lv_obj_t*, const char*, int, int, int, std::function<void(unsigned)>);
lv_obj_t* chart(lv_obj_t*, int, int, int, int);
}  // namespace detail
using namespace detail;
class ScopeWindow : public ui::Window {
public:
    static constexpr int kDialogWidth = 840, kDialogHeight = 490;
    std::function<bool()> can_close;
    void close(bool teleport = false, bool triggerCallback = true) override
    {
        if (teleport || !can_close || can_close()) ui::Window::close(teleport, triggerCallback);
    }
    explicit ScopeWindow(const char* title)
    {
        config.title       = title;
        config.closeBtn    = true;
        config.bgColor     = kPanel;
        config.borderColor = kLine;
        config.titleColor = kText;
        config.kfClosed    = {0, -(kHeight + kDialogHeight) / 2, kDialogWidth, kDialogHeight, 255};
        config.kfOpened    = {0, 0, kDialogWidth, kDialogHeight, 255};
    }
    void onInit() override
    {
        _window->setRadius(0);
        if (_title_label) _title_label->setTextFont(&vn_font_18);
        _anim_y.springOptions().visualDuration = .28;
        _anim_y.springOptions().bounce = 0;
    }
};
struct VibescopeView::Impl {
    ScopeRuntime _runtime;
    ScopeAnalysisWorker _analysis_worker;
    ScopeSnapshot _snapshot, _next_snapshot;
    std::unique_ptr<Container> _screen;
    lv_obj_t *_sidebar{}, *_status_bar{}, *_footer_bar{}, *_sidebar_toggle{}, *_sidebar_divider{}, *_compact_logo{};
    bool _sidebar_visible{true};
    lv_obj_t* _previous{};
    std::vector<std::unique_ptr<Button>> _buttons;
    std::unique_ptr<ScopeWindow> _window;
    lv_obj_t *_cal_step{}, *_cal_direction{}, *_cal_board_hint{}, *_cal_action{}, *_cal_state{};
    std::array<std::array<lv_obj_t*, 3>, 3> _cal_raw{};
    std::array<std::array<lv_obj_t*, 3>, 4> _cal_results{};
    std::array<lv_obj_t*, 6> _cal_faces{};
    lv_obj_t* _cal_progress{};
    Button *_cal_cancel{}, *_cal_save{};
    std::uint32_t _cal_updated{};
    std::array<PlotConfig, 3> _configs;
    struct Plot {
        lv_obj_t *panel{}, *title{}, *source{}, *wave{}, *empty{}, *image{}, *xunit{}, *yunit{};
        std::array<lv_obj_t*, 4> legend{};
        std::array<lv_obj_t*, 2> cursors{}, cursor_tags{};
        lv_obj_t* cursor_value{};
        lv_obj_t *trigger_marker{}, *trigger_line{};
        std::array<lv_obj_t*, 5> xticks{}, yticks{};
        std::array<lv_obj_t*, kMaxSpectrumPeaks> peak_dots{}, peak_labels{};
        ScopePlotPoints points{};
        SampleViewport viewport;
        SpectrumGrid spectrum_grid;
        SampleViewport frequency_band;
        std::uint32_t viewport_span{};
        Source viewport_source{Source::Adxl};
        PlotKind viewport_kind{PlotKind::Time};
        std::uint64_t viewport_generation{};
        std::uint64_t viewport_trigger_time{};
        lv_point_t press_point{};
        std::uint32_t pressed_at{};
        int cursor_grab_offset{};
        unsigned cursor_index{};
        std::size_t drag_first{};
        Axes cursor_means{};
        float trigger_sample_index{};
        float render_rate{};
        bool pressed{}, in_wave{}, cursor_grab{}, moved{};
        std::vector<std::uint16_t> wave_pixels;
        int wave_width{}, wave_height{}, wave_stride{};
        static constexpr unsigned kWaterfallRows = 32, kWaterfallBins = 128;
        std::array<std::array<std::uint8_t, kWaterfallBins>, kWaterfallRows> waterfall{};
        std::array<std::uint64_t, kWaterfallRows> times{};
        float waterfall_low_hz{}, waterfall_high_hz{};
        bool waterfall_ready{};
        std::uint64_t waterfall_bucket{}, waterfall_sample_ms{};
        std::uint64_t last_serial{}, generation{};
        PlotRenderState rendered;
        bool rendered_valid{};
    };
    std::array<Plot, 3> _plots;
    lv_obj_t *_status{}, *_record_status{}, *_record_dot{}, *_clock{}, *_temperature{};
    struct StatusIcon {
        lv_obj_t *root{}, *glyph{}, *slash{}, *badge{};
        int state{-1};
    };
    std::array<StatusIcon, 3> _status_icons;
    std::uint32_t _status_icon_tick{};
    lv_obj_t* _battery_percent{};
    lv_obj_t *_keyboard_icon{}, *_keyboard_focus{};
    std::uint32_t _keyboard_tick{};
    bool _keyboard_connected{};
    int _power_icon_key{-1};
    lv_obj_t *_metric_window{}, *_trend_chart{}, *_trend_scale{};
    lv_obj_t *_xyz_metrics{}, *_resultant_metrics{}, *_resultant_rms{}, *_resultant_trend{}, *_resultant_scale{};
    std::array<lv_obj_t*, kRmsBandCount> _resultant_bands{};
    std::array<lv_obj_t*, 4> _resultant_details{};
    bool _resultant_display{};
    std::array<lv_obj_t*, 3> _rms{}, _peak{}, _frequency{}, _crest{}, _kurtosis{};
    std::array<std::array<lv_obj_t*, 3>, kRmsBandCount> _band_rms{};
    unsigned _rendered_metric_source{2};
    std::uint64_t _rendered_metric_serial{}, _rendered_metric_generation{};
    static constexpr unsigned kResultantChannel = 3;
    static constexpr unsigned kXyzChannels = (1U << kResultantChannel) - 1;
    static constexpr unsigned kResultantChannels = 1U << kResultantChannel;
    using RmsHistory = std::array<std::vector<float>, kResultantChannel + 1>;
    struct TrendCanvas {
        std::vector<std::uint16_t> pixels;
        RmsHistory values;
        unsigned span{}, channels{};
        float maximum{.01F};
        int width{}, height{}, stride{};
        bool valid{}, grid{}, has_samples{};
    };
    TrendCanvas _trend_canvas, _resultant_canvas, _rms_canvas;
    std::array<lv_obj_t*, 2> _sidebar_trend_ticks{}, _sidebar_trend_hits{};
    unsigned _rms_trend_window{kDefaultRmsTrendWindow};
    lv_obj_t *_type{}, *_span{}, *_scale{}, *_scale_label{}, *_span_label{}, *_axes{}, *_range{}, *_layout_dropdown{};
    lv_obj_t *_frequency_controls{}, *_frequency_low{}, *_frequency_high{}, *_frequency_limit{};
    lv_obj_t *_peak_count{}, *_peak_label{}, *_waterfall_palette{}, *_palette_label{};
    lv_obj_t *_date_field{}, *_time_field{}, *_power_button{}, *_power_label{}, *_power_thumb{};
    lv_obj_t *_volume_slider{}, *_volume_label{}, *_brightness_slider{}, *_brightness_label{};
    hal::DevicePreferences _device_preferences;
    bool _device_preferences_dirty{}, _resume_after_sleep{};
    std::uint32_t _device_preferences_changed{};
    lv_obj_t* _sleep_overlay{};
    int _power_press_x{};
    std::uint32_t _shutdown_at{};
    int _shutdown_brightness{};
    std::atomic<bool> _shutdown_pending{false};
    Button *_run{}, *_record{}, *_cursor_button{}, *_trigger_button{}, *_screenshot_button{};
    lv_obj_t* _screenshot_flash{};
    std::uint32_t _screenshot_flash_at{};
    lv_obj_t* _trigger_keypad{};
    std::array<lv_obj_t*, 3> _navigation{};
    unsigned _record_duration_index{};
    int _trigger_threshold{150};
    unsigned _trigger_position{50};
    std::array<lv_obj_t*, 3> _angle_labels{};
    std::array<AnimateValue, 3> _angle_anim;
    lv_obj_t* _cube_canvas{};
    lv_obj_t *_pose_hit{}, *_probe_status{};
    AnimateValue _pose_flash;
    std::array<std::uint16_t, 112 * 100> _cube_pixels{};
    std::array<lv_obj_t*, 3> _cube_axis_labels{};
    Axes _reference{};
    bool _compass_mode{};
    lv_obj_t *_compass{}, *_compass_model{}, *_compass_summary{}, *_compass_strength{};
    lv_obj_t *_rms_panel{}, *_rms_plot{}, *_rms_hit{}, *_rms_source{}, *_rms_latest{}, *_rms_cursor_value{};
    std::array<lv_obj_t*, 5> _rms_time_ticks{}, _rms_value_ticks{};
    std::array<lv_obj_t*, 2> _rms_cursor_lines{}, _rms_cursor_tags{};
    PlotConfig _rms_cursor;
    lv_point_t _rms_press_point{};
    int _rms_grab_offset{};
    unsigned _rms_grab{};
    bool _rms_dragging{};
    lv_obj_t* _compass_strongest{};
    std::array<lv_obj_t*, 3> _compass_axis_labels{}, _compass_values{}, _compass_bars{};
    lv_obj_t* _compass_canvas{};
    std::unique_ptr<std::uint16_t, decltype(&std::free)> _compass_pixels{nullptr, &std::free};
    Axes _compass_angles{}, _compass_rms{};
    float _compass_scale{.1F};
    bool _compass_pose_valid{}, _compass_rms_valid{}, _compass_drawn{};
    bool _record_intent{}, _hold_waveforms{};
    std::uint32_t _press_at{}; // Record/screenshot hold timing; one touch at a time.
    lv_obj_t *_file_pressed{}, *_file_manager_request{};
    unsigned _selected{}, _layout_count{2}, _metric_source_index{};
    unsigned _metric_window_ms{1000}, _submitted_window_ms{};
    std::array<std::array<Analysis, 3>, 2> _analysis;
    std::array<bool, 2> _analysis_near_full_scale{};
    std::array<std::uint64_t, 2> _analysis_serial{}, _analysis_sample_time{};
    std::array<std::shared_ptr<const ScopeAnalysisOutput>, 2> _accepted_results;
    std::shared_ptr<const ScopeAnalysisOutput> _gyro_result;
    std::array<bool, 2> _submitted_resultant_metrics{};
    std::array<bool, 3> _submitted_resultant_time{};
    std::uint64_t _submitted_gyro_serial{};
    std::array<std::uint64_t, 2> _submitted_serial{}, _submitted_generation{};
    std::array<float, 2> _submitted_rate{};
    std::array<std::size_t, 2> _submitted_spectrum_bins{};
    std::array<bool, 2> _submitted_valid{};
    bool _analysis_submitted{};
    std::uint64_t _analysis_cost_us{};
    std::array<RmsHistory, 2> _rms_history;
    std::uint32_t _updated{}, _trend_tick{};
    std::uint32_t _metrics_draw_tick{};
    std::uint32_t _pose_draw_tick{};
    std::uint32_t _perf_tick{}, _perf_updates{};
    std::uint64_t _perf_copy{}, _perf_analysis{}, _perf_apply{}, _perf_max{};
    std::uint64_t _render_started{}, _render_sum{}, _render_max{}, _flush_started{}, _flush_sum{}, _flush_max{};
    std::uint64_t _wait_started{}, _wait_sum{}, _wait_max{};
    unsigned _render_frames{}, _render_flushes{};
    std::uint64_t _flush_pixels{}, _section_us[5]{};
    PpaStats _last_gpu{};
    std::array<std::uint64_t, 2> _trend_serial{}, _trend_generation{};
    Axes _pose_rendered{};
    bool _pose_rendered_valid{}, _pose_rendered_initialized{};
    int _detail_kind{};
    lv_obj_t *_record_duration{}, *_file_list{}, *_file_status{}, *_file_delete{},
             *_file_prev{}, *_file_next{}, *_file_page_label{};
    std::shared_ptr<const RecordFilesResult> _visible_files;
    std::shared_ptr<const RecordFilesResult> _export_baseline;
    lv_obj_t* _preview{};
    std::shared_ptr<const RecordFilesResult> _preview_files; // Owns the shown pixels.
    std::string _preview_name; // Set on filename click, submitted by update_file_list().
    bool _preview_awaiting{}, _preview_draw_pending{};
    std::uint32_t _preview_draw_started{};
    lv_image_dsc_t _preview_dsc{};
    lv_obj_t *_device_export{}, *_device_export_status{}, *_device_sd_usage{};
    lv_obj_t* _device_sd_bar{};
    bool _device_export_started{};
    std::vector<std::string> _checked_files;
    std::size_t _file_page{};
    bool _delete_armed{}, _file_scan_pending{};
    std::string _last_message;
    ScopePreferences::Values _last_preferences{};
    bool _preferences_edited{};

    Button* button(lv_obj_t* parent, const char* value, int x, int y, int w, int h, std::function<void()> callback,
                   unsigned color = kSoft)
    {
        auto ptr = std::make_unique<Button>(parent);
        auto* b  = ptr.get();
        b->setPos(x, y);
        b->setSize(w, h);
        b->setBgColor(lv_color_hex(color));
        b->setRadius(5);
        b->setShadowWidth(0);
        b->setBorderWidth(1);
        b->setBorderColor(lv_color_hex(kLine));
        b->label().setText(value);
        b->label().setTextFont(&vn_font_16);
        b->label().setTextColor(lv_color_hex(color == kAccent ? kOnAccent : kText));
        b->onClick().connect(std::move(callback));
        _buttons.push_back(std::move(ptr));
        return b;
    }
    void init();
    static void display_event(lv_event_t* event);
    void update();
    void update_snapshot();
    void update_frame(std::uint32_t now);
    void update_analysis(std::uint32_t now);
    void build_sidebar();
    void build_status();
    void layout_status();
    void update_status_icons();
    void update_keyboard(std::uint32_t now);
    bool handle_keyboard_press(const hal::KeyboardPress&);
    bool handle_window_key(unsigned key, std::uint8_t modifiers);
    void focus_keyboard_control(lv_obj_t* control);
    void show_keyboard_help();
    void toggle_recording();
    void set_status_icon(StatusIcon& icon, hal::ConnectionState state);
    void build_footer();
    static void file_button_event(lv_event_t*);
    void capture_screenshot();
    void show_trigger_keypad(lv_obj_t* input);
    lv_obj_t* numeric_field(lv_obj_t* parent, int x, int y, int width, unsigned length);
    void refresh_frequency_fields();
    void update_frequency_limit();
    void apply_frequency_fields();
    void show_device_settings();
    void apply_device_time();
    void update_device_export();
    void device_preferences_changed();
    bool save_device_preferences();
    void service_idle_power();
    void wake_from_sleep();
    void begin_shutdown();
    void service_shutdown();
    void close_trigger_keypad();
    ScopePreferences current_preferences() const;
    bool sync_preferences();
    void set_acquisition_running(bool run);
    int content_left() const { return _sidebar_visible ? kSide : 0; }
    void toggle_sidebar();
    void layout_sidebar();
    void layout_plots();
    void select(unsigned index);
    void render_plot(unsigned index);
    void render_spectrum_peaks(Plot& plot, const std::array<const Analysis*, 3>& spectra,
                               unsigned limit, float bin_hz, float minimum, float maximum, bool db, bool combined);
    bool uses_resultant(const PlotConfig& config) const;
    const ScopeAnalysisOutput* resultant_frame(unsigned source) const;
    const std::vector<float>& resultant_samples(unsigned source) const;
    const Analysis& plot_analysis(const PlotConfig& config, unsigned axis) const;
    std::size_t plot_data_count(const PlotConfig& config) const;
    void set_resultant_display(bool enabled);
    void render_cursor(unsigned index);
    static void plot_event(lv_event_t* event);
    void navigate(unsigned index, unsigned action);
    void render_metrics();
    void render_pose();
    void bind_pose_reset(lv_obj_t* object);
    void reset_pose();
    void toggle_compass();
    void layout_compass();
    void render_compass();
    void draw_compass();
    void open_window(const char* title, int kind);
    void show_rms_trend_config();
    void layout_rms_trend(int width);
    void render_rms_trend();
    void render_rms_cursor();
    static void rms_plot_event(lv_event_t* event);
    PlotConfig& cursor_config() { return _compass_mode ? _rms_cursor : _configs[_selected]; }
    void show_about();
    void show_waveform_config();
    void show_cursor_config();
    void show_measurement_settings();
    void show_calibration();
    void update_calibration();
    void show_trigger_config();
    void show_record_config();
    void show_screenshot_files();
    void build_file_browser(int list_y);
    void update_file_list();
    void render_file_page();
    void show_preview(const std::shared_ptr<const RecordFilesResult>& owner);
    void close_preview();
    void close_window();
    lv_obj_t* trend_canvas(lv_obj_t* parent, int x, int y, int w, int h, TrendCanvas& cache, bool grid);
    void trend(lv_obj_t* target, TrendCanvas& cache, const RmsHistory& values, unsigned channels,
               unsigned background);
    ~Impl()
    {
        LvglLockGuard lock;
        if (_sleep_overlay) GetHAL()->setDisplayBrightness(_device_preferences.brightness);
        close_window();
        lv_display_remove_event_cb_with_user_data(lv_display_get_default(), display_event, this);
        if (_previous && lv_obj_is_valid(_previous)) lv_screen_load(_previous);
        _buttons.clear();
        _screen.reset();
    }
};

}  // namespace vibescope_view
