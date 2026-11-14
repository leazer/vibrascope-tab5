/**
 * @file view_windows.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "view_private.h"
#include "../scope_keyboard.h"
#include <app.h>
#include <cstdlib>
#include <cstring>

extern "C" {
LV_IMAGE_DECLARE(vn_about_digikey);
LV_IMAGE_DECLARE(vn_about_eeworld);
LV_IMAGE_DECLARE(vn_about_probe);
}

namespace vibescope_view {
void VibescopeView::Impl::close_window()
{
    focus_keyboard_control(nullptr);
    if (_cal_step) _runtime.cancel_calibration();
    _cal_step = _cal_direction = _cal_board_hint = _cal_action = _cal_state = _cal_progress = nullptr;
    _cal_cancel = _cal_save = nullptr;
    _cal_raw = {};
    _cal_results = {};
    _cal_faces = {};
    close_trigger_keypad();
    _type = _span = _scale = _scale_label = _span_label = _axes = _range = nullptr;
    _frequency_controls = _frequency_low = _frequency_high = _frequency_limit = nullptr;
    _peak_count = _peak_label = _palette_label = _waterfall_palette = nullptr;
    _date_field = _time_field = _power_button = _power_label = _power_thumb = nullptr;
    _volume_slider = _volume_label = _brightness_slider = _brightness_label = nullptr;
    _record_duration = _file_list = _file_status = _file_delete = nullptr;
    _device_export = _device_export_status = _device_sd_usage = nullptr;
    _device_sd_bar = nullptr;
    _device_export_started = false;
    _export_baseline.reset();
    _file_prev = _file_next = _file_page_label = nullptr;
    _visible_files.reset();
    _checked_files.clear();
    _preview_awaiting = false;
    _preview_name.clear();
    close_preview();
    _detail_kind   = 0;
    _window.reset();
}
void VibescopeView::Impl::open_window(const char* title, int kind)
{
    close_window();
    _detail_kind = kind;
    _window      = std::make_unique<ScopeWindow>(title);
    _window->init(_screen->get());
}
void VibescopeView::Impl::show_rms_trend_config()
{
    open_window("总 RMS 趋势设置", 1);
    auto* content = _window->get()->get();
    text(content, "X 轴时长", 70, 106, kMuted);
    auto* span = dropdown(content, "15 s\n30 s\n60 s\n2 min\n5 min", 290, 90, 300, [this](unsigned value) {
        _rms_trend_window = value;
        _metrics_draw_tick = GetHAL()->millis() - 200;
        render_metrics();
        render_rms_trend();
    });
    lv_dropdown_set_selected(span, _rms_trend_window);
    _window->open();
}

void VibescopeView::Impl::show_about()
{
    open_window("关于", 2);
    auto* content = _window->get()->get();
    const auto place_image = [content](const lv_image_dsc_t& source, int x, int y) {
        auto* image = lv_image_create(content);
        lv_image_set_src(image, &source);
        lv_obj_set_pos(image, x, y);
    };

    constexpr int left = 76;
    constexpr int logo_top = 104;
    place_image(vn_logo, left, logo_top);
    auto* version = panel(content, left + vn_logo.header.w + 18, logo_top + 10, 52, 24, kSoft, LV_RADIUS_CIRCLE);
    lv_obj_center(text(version, app::kFirmwareVersion, 0, 0, kText, &lv_font_montserrat_14));

    text(content, "实时采集三轴振动与探头姿态。", left, 188, kText, &vn_font_18);
    text(content, "支持波形、频谱、瀑布图、趋势分析与录制。", left, 222);
    text(content, "Creator : 煲仔卤煮", left, 277, kMuted, &vn_font_18);

    place_image(vn_about_digikey, left, 330);
    text(content, "得捷", left, 365, 0xff4535);
    place_image(vn_about_eeworld, left + 156, 329);

    constexpr int photo_left = 456, photo_top = 90, photo_width = 320, photo_height = 326;
    place_image(vn_about_probe, photo_left + (photo_width - vn_about_probe.header.w) / 2,
                photo_top + (photo_height - vn_about_probe.header.h) / 2);
    _window->open();
}
void VibescopeView::Impl::show_measurement_settings()
{
    open_window("探头设置", 6);
    auto* content = _window->get()->get();
    text(content, "数据源", 70, 104, kMuted);
    text(content, "加速度量程", 70, 184, kMuted);
    text(content, "统计窗", 70, 264, kMuted);
    auto update_range = [this] {
        const auto source = static_cast<Source>(_metric_source_index);
        lv_dropdown_set_options(_range, source == Source::Adxl ?
            "±10 g\n±20 g\n±40 g" : "±2 g\n±4 g\n±8 g\n±16 g");
        lv_dropdown_set_selected(_range, _runtime.accel_range_index(source));
    };
    auto* source = dropdown(content, "ADXL357\nICM42688", 290, 88, 300, [this, update_range](unsigned value) {
        _metric_source_index = value;
        update_range();
        select(_selected);
    });
    lv_dropdown_set_selected(source, _metric_source_index);
    _range = dropdown(content, "", 290, 168, 300, [this](unsigned value) {
        _runtime.set_accel_range(static_cast<Source>(_metric_source_index), value);
    });
    update_range();
    auto* window = dropdown(content, "0.5 s\n1 s\n2 s", 290, 248, 300, [this](unsigned value) {
        _metric_window_ms = kStatisticsWindowsMs[value];
    });
    for (unsigned i = 0; i < kStatisticsWindowsMs.size(); ++i)
        if (kStatisticsWindowsMs[i] == _metric_window_ms) lv_dropdown_set_selected(window, i);
    text(content, "数据显示", 70, 342, kMuted);
    auto* display = dropdown(content, "XYZ\n合成矢量", 290, 326, 300, [this](unsigned value) {
        set_resultant_display(value == 1);
        select(_selected);
    });
    lv_dropdown_set_selected(display, _resultant_display ? 1 : 0);
    text(content, "传感器标定", 70, 422, kMuted);
    button(content, "开始", 290, 407, 300, 43, [this] { show_calibration(); });
    _window->open();
}

namespace {
constexpr const char* calibration_faces[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
constexpr const char* calibration_board_faces[] = {"+Y", "-Y", "+X", "-X", "+Z", "-Z"};
constexpr unsigned kCalGreen = 0x83d8a8;
}

void VibescopeView::Impl::show_calibration()
{
    if (const char* error = _runtime.begin_calibration()) {
        ui::pop_a_toast(error, ui::toast_type::error, 2500, &vn_font_16, 0);
        return;
    }
    open_window("六面标定", 0);
    _window->can_close = [this] {
        if (_runtime.calibration_view().stage == CalibrationStage::Saving) return false;
        _runtime.cancel_calibration();
        return true;
    };
    auto* content = _window->get()->get();
    _cal_step = text(content, "", 32, 58, kAccent);
    text(content, "ADXL357 + ICM42688", 263, 58, kMuted, &lv_font_montserrat_16);
    if (_snapshot.demo) text(content, "模拟演示", 718, 58, kMuted);

    auto* guide = panel(content, 32, 96, 306, 259, kPlot, 7);
    auto* arrow = text(guide, LV_SYMBOL_UP, 0, 0, kAccent, &lv_font_montserrat_24);
    lv_obj_align(arrow, LV_ALIGN_TOP_MID, 0, 16);
    auto* face = panel(guide, 96, 56, 114, 52, kSelected, 5);
    _cal_direction = text(face, "", 0, 0, kAccent, &lv_font_montserrat_24);
    lv_obj_align(_cal_direction, LV_ALIGN_CENTER, 0, 0);
    text(guide, "传感器方向朝上", 92, 121);
    _cal_board_hint = text(guide, "", 17, 156, kMuted);
    text(guide, "用平面支撑探头，线缆留松", 17, 182, kMuted);
    text(guide, "每面依次采集两颗加速度计", 17, 219, kMuted);

    auto* readings = panel(content, 354, 96, 454, 259, kPlot, 7);
    text(readings, "原始读数", 16, 13);
    constexpr int first_axis = 187, axis_spacing = 84;
    for (unsigned axis = 0; axis < 3; ++axis)
        text(readings, axis_names[axis], first_axis + axis * axis_spacing, 13, kAxes[axis], &lv_font_montserrat_16);
    constexpr const char* raw_labels[] = {"ADXL357 加速度 g", "ICM42688 加速度 g", "ICM42688 角速度 °/s"};
    for (unsigned row = 0; row < _cal_raw.size(); ++row) {
        const auto y = 41 + row * 25;
        text(readings, raw_labels[row], 16, y, kMuted);
        for (unsigned axis = 0; axis < 3; ++axis)
            _cal_raw[row][axis] = text(readings, "--", first_axis + axis * axis_spacing, y,
                                       kText, &lv_font_montserrat_16);
    }
    panel(readings, 16, 120, 422, 1, kLine);
    text(readings, "标定结果", 16, 127);
    constexpr const char* result_labels[] = {
        "ADXL357 偏置 mg", "ADXL357 比例系数", "ICM42688 偏置 mg", "ICM42688 比例系数"};
    for (unsigned row = 0; row < _cal_results.size(); ++row) {
        const auto y = 152 + row * 24;
        text(readings, result_labels[row], 16, y, kMuted);
        for (unsigned axis = 0; axis < 3; ++axis)
            _cal_results[row][axis] = text(readings, "--", first_axis + axis * axis_spacing, y,
                                           kText, &lv_font_montserrat_16);
    }
    _cal_action = text(content, "", 32, 365);
    _cal_state = text(content, "", 32, 389, kMuted);
    _cal_progress = lv_bar_create(content);
    position(_cal_progress, 32, 416, 776, 5);
    lv_bar_set_range(_cal_progress, 0, 6);
    lv_obj_set_style_bg_color(_cal_progress, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_cal_progress, lv_color_hex(kAccent), LV_PART_INDICATOR);
    for (unsigned i = 0; i < _cal_faces.size(); ++i)
        _cal_faces[i] = text(content, calibration_faces[i], 32 + i * 50, 444, kMuted, &lv_font_montserrat_16);

    _cal_cancel = button(content, "取消标定", 506, 431, 141, 44, [this] {
        if (_runtime.calibration_view().stage == CalibrationStage::Saving) return;
        _runtime.cancel_calibration();
        show_measurement_settings();
    });
    _cal_save = button(content, "保存到探头", 659, 431, 149, 44, [this] { _runtime.save_calibration(); }, kAccent);
    update_calibration();
    _window->open();
}

void VibescopeView::Impl::update_calibration()
{
    const auto view = _runtime.calibration_view();
    const auto stage = view.stage;
    const unsigned face = view.step.face;
    const char* sensor = source_names[view.step.sensor];
    const bool all_faces = view.result_ready;
    const bool finished = stage == CalibrationStage::Saved || stage == CalibrationStage::Failed ||
                          stage == CalibrationStage::Cancelled;
    char line[128];
    std::snprintf(line, sizeof(line), "第 %u / 6 面", face + 1);
    set_text(_cal_step, all_faces ? "六面已完成" : line);
    set_text(_cal_direction, calibration_faces[face]);
    std::snprintf(line, sizeof(line), "本版 PCB 丝印 %s 朝上", calibration_board_faces[face]);
    set_text(_cal_board_hint, line);
    for (unsigned axis = 0; axis < 3; ++axis) {
        for (unsigned i = 0; i < view.accel.size(); ++i) {
            if (view.accel_live[i])
                std::snprintf(line, sizeof(line), "%+.3f", static_cast<double>(view.accel[i][axis]));
            set_text(_cal_raw[i][axis], view.accel_live[i] ? line : "--");
        }
        if (view.gyro_live) std::snprintf(line, sizeof(line), "%+.2f", static_cast<double>(view.gyro[axis]));
        set_text(_cal_raw[2][axis], view.gyro_live ? line : "--");
    }
    const auto& record = view.result;
    const Axes* results[] = {&record.adxl_accel_bias_g, &record.adxl_accel_scale,
                             &record.icm_accel_bias_g, &record.icm_accel_scale};
    for (unsigned row = 0; row < _cal_results.size(); ++row) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (view.result_ready) {
                const double value = (*results[row])[axis];
                if (row % 2 == 0) std::snprintf(line, sizeof(line), "%+.1f", value * 1000.0);
                else std::snprintf(line, sizeof(line), "%.4f", value);
            }
            set_text(_cal_results[row][axis], view.result_ready ? line : "--");
        }
    }

    char action_line[128];
    const char* action = "";
    const char* state = "";
    unsigned color = kMuted;
    switch (stage) {
    case CalibrationStage::Preparing:
        action = "正在准备标定";
        state = "请把探头放在稳固的平面上";
        break;
    case CalibrationStage::Place:
        std::snprintf(action_line, sizeof(action_line), view.retry ? "%s 采集未通过，请重新放稳" :
                      "放稳后自动采集 %s", sensor);
        action = action_line;
        if (!view.accel_live[0] || !view.accel_live[1] || !view.gyro_live) state = "等待原始读数";
        else if (!view.aligned) state = "方向未到位";
        else if (!view.still) state = "方向到位，请保持静止";
        else {
            std::snprintf(line, sizeof(line), "方向到位  |  稳定剩余 %.1f s", ((view.remaining_ms + 99) / 100) / 10.0);
            state = line;
            color = kCalGreen;
        }
        break;
    case CalibrationStage::Capture:
        std::snprintf(action_line, sizeof(action_line), "正在采集 %s，请勿移动探头", sensor);
        action = action_line;
        std::snprintf(line, sizeof(line), "%s  |  采集剩余约 %.1f s",
                      view.still ? "方向到位" : "请保持静止",
                      ((view.remaining_ms + 99) / 100) / 10.0);
        state = line;
        color = view.still ? kCalGreen : kAccent;
        break;
    case CalibrationStage::Review:
        action = "两颗加速度计的六面采集均已通过";
        state = _snapshot.demo ? "模拟演示结束，点击保存查看成功提示" : "请确认保存，取消将退出并保留原标定";
        color = kCalGreen;
        break;
    case CalibrationStage::Saving:
        action = "正在保存到探头";
        state = "请保持 USB 连接";
        break;
    case CalibrationStage::Restoring:
        action = "正在退出标定";
        break;
    case CalibrationStage::Saved:
        action = _snapshot.demo ? "模拟标定成功" : "标定成功，已保存到探头";
        state = view.error ? view.error : "";
        color = kCalGreen;
        break;
    case CalibrationStage::Failed:
        action = "标定未完成";
        state = view.error ? view.error : "";
        color = kRed;
        break;
    case CalibrationStage::Cancelled:
        action = "已取消标定";
        break;
    case CalibrationStage::Idle:
        break;
    }
    set_text(_cal_action, action);
    set_text(_cal_state, state);
    set_text_color(_cal_state, color);
    const unsigned complete = all_faces ? 6 : face;
    lv_bar_set_value(_cal_progress, complete, LV_ANIM_OFF);
    for (unsigned i = 0; i < _cal_faces.size(); ++i)
        set_text_color(_cal_faces[i], i < complete ? kCalGreen : i == complete ? kAccent : kMuted);
    set_text(_cal_cancel->label().get(), finished ? "返回设置" : "取消标定");
    if (stage == CalibrationStage::Saving) lv_obj_add_state(_cal_cancel->get(), LV_STATE_DISABLED);
    else lv_obj_remove_state(_cal_cancel->get(), LV_STATE_DISABLED);
    if (stage == CalibrationStage::Review) lv_obj_remove_flag(_cal_save->get(), LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(_cal_save->get(), LV_OBJ_FLAG_HIDDEN);
}

void VibescopeView::Impl::show_waveform_config()
{
    open_window("波形设置", 4);
    auto* content = _window->get()->get();
    text(content, "类型", 54, 92, kMuted);
    _type = dropdown(content, "时域波形\n幅值频谱\n瀑布图\nGyro 角速度", 180, 76, 250, [this](unsigned value) {
        auto& config = _configs[_selected];
        config.kind = static_cast<PlotKind>(value);
        if (config.kind == PlotKind::Waterfall && config.axis_mode == PlotAxisMode::XYZ)
            config.axis_mode = PlotAxisMode::Follow;
        _plots[_selected].waterfall_ready = false;
        select(_selected);
        refresh_frequency_fields();
    });
    text(content, "显示轴", 54, 168, kMuted);
    _axes = dropdown(content, "", 180, 152, 250, [this](unsigned value) {
        auto& config = _configs[_selected];
        if (config.kind == PlotKind::Waterfall && value) ++value;
        config.axis_mode = static_cast<PlotAxisMode>(value);
        _plots[_selected].waterfall_ready = false;
        select(_selected);
    });
    _scale_label = text(content, "幅值", 54, 244, kMuted);
    _scale = dropdown(content, "自动\n固定", 180, 228, 250, [this](unsigned value) {
        auto& config = _configs[_selected];
        config.auto_scale = value != 1;
        config.spectrum_db_scale = config.kind == PlotKind::Spectrum && value == 2;
        select(_selected);
    });
    _palette_label = text(content, "配色", 54, 244, kMuted);
    _waterfall_palette = dropdown(content, "黄黑\n蓝红热图\n紫绿黄", 180, 228, 250, [this](unsigned value) {
        _configs[_selected].waterfall_palette = static_cast<WaterfallPalette>(value);
        render_plot(_selected);
    });
    _span_label = text(content, "时窗", 54, 320, kMuted);
    _span = dropdown(content, "", 180, 304, 250, [this](unsigned value) {
        auto& config = _configs[_selected];
        if (config.kind == PlotKind::Waterfall) {
            config.waterfall_window = value;
            _plots[_selected].waterfall_ready = false;
        } else {
            config.span_ms = kWaveformWindowsMs[value];
        }
        select(_selected);
    });
    _peak_label = text(content, "峰值标注", 54, 320, kMuted);
    _peak_count = dropdown(content, "关闭\n最多 1 个\n最多 2 个\n最多 3 个", 180, 304, 250,
                           [this](unsigned value) {
        _configs[_selected].spectrum_peak_count = value;
        render_plot(_selected);
    });

    _frequency_controls = panel(content, 453, 76, 347, 222, kSoft, 5);
    text(_frequency_controls, "显示频段 / Hz", 14, 12);
    text(_frequency_controls, "下限", 14, 51, kMuted);
    text(_frequency_controls, "上限", 180, 51, kMuted);
    _frequency_low = numeric_field(_frequency_controls, 14, 77, 151, 4);
    _frequency_high = numeric_field(_frequency_controls, 180, 77, 151, 4);
    _frequency_limit = text(_frequency_controls, "", 14, 135, kRed);
    auto* apply = lv_button_create(_frequency_controls);
    surface(apply, kPanel, 5);
    position(apply, 14, 166, 151, 44);
    lv_obj_center(text(apply, "应用频段", 0, 0));
    lv_obj_add_event_cb(apply, [](lv_event_t* event) {
        static_cast<Impl*>(lv_event_get_user_data(event))->apply_frequency_fields();
    }, LV_EVENT_CLICKED, this);
    auto* reset = lv_button_create(_frequency_controls);
    surface(reset, kPanel, 5);
    position(reset, 180, 166, 151, 44);
    lv_obj_center(text(reset, "恢复默认", 0, 0));
    lv_obj_add_event_cb(reset, [](lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        lv_textarea_set_text(self->_frequency_low, "0");
        lv_textarea_set_text(self->_frequency_high, "");
        self->apply_frequency_fields();
    }, LV_EVENT_CLICKED, this);
    select(_selected);
    refresh_frequency_fields();
    _window->open();
}

void VibescopeView::Impl::refresh_frequency_fields()
{
    const auto& config = _configs[_selected];
    char value[48];
    std::snprintf(value, sizeof(value), "%u", config.spectrum_low_hz);
    lv_textarea_set_text(_frequency_low, value);
    std::snprintf(value, sizeof(value), "%u", config.spectrum_high_hz);
    lv_textarea_set_text(_frequency_high, config.spectrum_high_hz ? value : "");
    set_text(_frequency_limit, "");
    update_frequency_limit();
}

void VibescopeView::Impl::update_frequency_limit()
{
    const auto source = static_cast<unsigned>(_configs[_selected].source);
    const auto rate = _snapshot.rate[source];
    char value[16] = "--";
    if (rate > 0) std::snprintf(value, sizeof(value), "%.0f", rate * .5F);
    const auto* current = lv_textarea_get_placeholder_text(_frequency_high);
    if (!current || std::strcmp(current, value) != 0)
        lv_textarea_set_placeholder_text(_frequency_high, value);
}

void VibescopeView::Impl::apply_frequency_fields()
{
    const auto* high_text = lv_textarea_get_text(_frequency_high);
    const auto low = static_cast<unsigned>(std::strtoul(lv_textarea_get_text(_frequency_low), nullptr, 10));
    const auto high = static_cast<unsigned>(std::strtoul(high_text, nullptr, 10));
    auto& config = _configs[_selected];
    const auto nyquist = _snapshot.rate[static_cast<unsigned>(config.source)] * .5F;
    if ((nyquist > 0 && (low >= nyquist || high > std::round(nyquist))) ||
        low > 8000 || high > 8000 || (*high_text && high <= low)) {
        set_text(_frequency_limit, "请输入有效的上下限");
        return;
    }
    config.spectrum_low_hz = low;
    config.spectrum_high_hz = high;
    auto& plot = _plots[_selected];
    plot.frequency_band = {};
    plot.rendered_valid = false;
    plot.waterfall_ready = false;
    render_plot(_selected);
    refresh_frequency_fields();
}

lv_obj_t* VibescopeView::Impl::numeric_field(lv_obj_t* parent, int x, int y, int width, unsigned length)
{
    auto* field = lv_textarea_create(parent);
    position(field, x, y, width, 48);
    lv_textarea_set_one_line(field, true);
    lv_textarea_set_accepted_chars(field, "0123456789");
    lv_textarea_set_max_length(field, length);
    lv_obj_set_style_text_font(field, &vn_font_18, 0);
    lv_obj_set_style_bg_color(field, lv_color_hex(kPlot), 0);
    lv_obj_set_style_text_color(field, lv_color_hex(kText), 0);
    border(field);
    lv_obj_add_event_cb(field, [](lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        self->show_trigger_keypad(lv_event_get_current_target_obj(event));
    }, LV_EVENT_CLICKED, this);
    return field;
}

void VibescopeView::Impl::show_cursor_config()
{
    open_window("游标设置", 7);
    auto* content = _window->get()->get();
    text(content, "游标数量", 70, 106, kMuted);
    auto* mode = dropdown(content, "单根\n两根", 290, 90, 300, [this](unsigned value) {
        cursor_config().dual_cursor = value == 1;
        select(_selected);
    });
    lv_dropdown_set_selected(mode, cursor_config().dual_cursor ? 1 : 0);
    if (_compass_mode) {
        _window->open();
        return;
    }
    text(content, "游标数据轴", 70, 186, kMuted);
    const bool combined = uses_resultant(_configs[_selected]);
    auto* axis = dropdown(content, combined ? "合成矢量" : "X\nY\nZ", 290, 170, 300, [this](unsigned value) {
        _configs[_selected].cursor_axis = value;
        select(_selected);
    });
    const auto& config = _configs[_selected];
    const bool multi_axis = config.all_axes && config.kind != PlotKind::Waterfall;
    lv_dropdown_set_selected(axis, combined ? 0 : multi_axis ? config.cursor_axis : config.axis);
    lv_obj_set_state(axis, LV_STATE_DISABLED, !multi_axis);
    lv_obj_set_style_opa(axis, LV_OPA_50, LV_STATE_DISABLED);
    _window->open();
}

namespace {
constexpr int kFileListWidth = 776;
constexpr int kFileColumns = 2, kFileRows = 5;
constexpr int kFileColumnWidth = kFileListWidth / kFileColumns;
constexpr std::size_t kFilesPerPage = kFileColumns * kFileRows;
const char* file_basename(const std::string& name)
{
    const auto separator = name.find_last_of("/\\");
    return separator == std::string::npos ? name.c_str() : name.c_str() + separator + 1;
}
}

// Shared list/status/pager/delete block for the recording and screenshot windows.
// _detail_kind==5 browses waveform recordings, 9 browses SCREENS/ screenshots.
void VibescopeView::Impl::build_file_browser(int list_y)
{
    auto* content = _window->get()->get();
    _file_list = panel(content, 32, list_y, kFileListWidth, 200, kPlot, 4);
    _file_status = text(content, "", 32, list_y + 214, kMuted);
    _file_page_label = text(content, "", 220, 432, kMuted);
    auto action = [&](const char* title, int x, int w, lv_event_cb_t cb) {
        auto* control = lv_button_create(content);
        surface(control, kSoft, 5);
        position(control, x, 412, w, 46);
        auto* caption = text(control, title, 0, 0);
        lv_obj_center(caption);
        lv_obj_add_event_cb(control, cb, LV_EVENT_CLICKED, this);
        lv_obj_set_style_opa(control, LV_OPA_50, LV_STATE_DISABLED);
        return control;
    };
    _file_prev = action("上一页", 32, 84, [](lv_event_t* e) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(e));
        if (self->_file_page) --self->_file_page;
        self->_delete_armed = false;
        self->render_file_page();
    });
    _file_next = action("下一页", 126, 84, [](lv_event_t* e) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(e));
        ++self->_file_page;
        self->_delete_armed = false;
        self->render_file_page();
    });
    _file_delete = action("删除", 672, 136, [](lv_event_t* e) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(e));
        if (!self->_delete_armed) {
            self->_delete_armed = true;
            self->update_file_list();
            return;
        }
        if (self->_runtime.request_record_files(RecordFilesAction::Delete, self->_checked_files,
                                                self->_detail_kind == 9)) {
            GetHAL()->playDeleteVoice();
            self->_delete_armed = false;
            self->_checked_files.clear();
            self->render_file_page();
        }
    });
}

void VibescopeView::Impl::show_record_config()
{
    open_window("录制设置", 5);
    auto* content = _window->get()->get();
    text(content, "停止方式", 32, 77, kMuted);
    _record_duration = dropdown(content, "手动停止\n1 min\n5 min\n10 min\n30 min", 155, 62, 232,
        [this](unsigned value) {
            _record_duration_index = std::min(value, 4U);
            _runtime.set_record_duration(kRecordDurationsSeconds[_record_duration_index]);
        });
    lv_dropdown_set_selected(_record_duration, _record_duration_index);
    text(content, "波形文件", 32, 129);
    build_file_browser(156);
    _file_page = 0;
    _delete_armed = false;
    _file_scan_pending = true;
    update_file_list();
    _window->open();
}

void VibescopeView::Impl::show_screenshot_files()
{
    open_window("截屏管理", 9);
    build_file_browser(84);
    _file_page = 0;
    _delete_armed = false;
    _file_scan_pending = true;
    update_file_list();
    _window->open();
}

void VibescopeView::Impl::render_file_page()
{
    if (_keyboard_focus && lv_obj_is_valid(_keyboard_focus) && lv_obj_get_parent(_keyboard_focus) == _file_list)
        focus_keyboard_control(nullptr);
    lv_obj_clean(_file_list);
    const auto total = _visible_files ? _visible_files->names.size() : 0;
    const auto pages = std::max<std::size_t>(1, (total + kFilesPerPage - 1) / kFilesPerPage);
    _file_page = std::min(_file_page, pages - 1);
    const auto first = _file_page * kFilesPerPage;
    const bool screens = _detail_kind == 9;
    for (auto i = first; i < std::min(total, first + kFilesPerPage); ++i) {
        const auto& name = _visible_files->names[i];
        const auto cell = static_cast<int>(i - first);
        const int x = 12 + cell % kFileColumns * kFileColumnWidth;
        const int y = 5 + cell / kFileColumns * 38;
        // Recordings may carry a RECORDS/ prefix on disk; the list shows the
        // bare file name and keeps the full name for selection/delete.
        const auto* display = file_basename(name);
        auto* checkbox = lv_checkbox_create(_file_list);
        lv_checkbox_set_text(checkbox, screens ? "" : display);
        constexpr int checkbox_width = 42, cell_padding = 24;
        position(checkbox, x, y, screens ? checkbox_width : kFileColumnWidth - cell_padding, 35);
        lv_obj_set_style_text_font(checkbox, &vn_font_16, 0);
        lv_obj_set_style_text_color(checkbox, lv_color_hex(kText), 0);
        lv_obj_set_style_bg_color(checkbox, lv_color_hex(kAccent),
                                  static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
        lv_obj_set_state(checkbox, LV_STATE_CHECKED,
                         std::find(_checked_files.begin(), _checked_files.end(), name) != _checked_files.end());
        // File index instead of the checkbox text: screenshot cells keep the name
        // on a separate label and their checkbox text is empty.
        lv_obj_set_user_data(checkbox, reinterpret_cast<void*>(i));
        lv_obj_add_event_cb(checkbox, [](lv_event_t* e) {
            auto* self = static_cast<Impl*>(lv_event_get_user_data(e));
            const auto index = reinterpret_cast<std::size_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(e)));
            if (!self->_visible_files || index >= self->_visible_files->names.size()) return;
            auto* obj = lv_event_get_current_target_obj(e);
            const auto& name = self->_visible_files->names[index];
            const auto existing = std::find(self->_checked_files.begin(), self->_checked_files.end(), name);
            if (lv_obj_has_state(obj, LV_STATE_CHECKED)) {
                if (existing == self->_checked_files.end()) self->_checked_files.emplace_back(name);
            } else if (existing != self->_checked_files.end())
                self->_checked_files.erase(existing);
            self->_delete_armed = false;
            self->update_file_list();
        }, LV_EVENT_VALUE_CHANGED, this);
        if (!screens) continue;
        auto* name_label = lv_label_create(_file_list);
        lv_label_set_text(name_label, display);
        position(name_label, x + checkbox_width, y, kFileColumnWidth - checkbox_width - cell_padding, 35);
        lv_obj_set_style_text_font(name_label, &vn_font_16, 0);
        lv_obj_set_style_text_color(name_label, lv_color_hex(kText), 0);
        lv_obj_add_flag(name_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(name_label, reinterpret_cast<void*>(i));
        lv_obj_add_event_cb(name_label, [](lv_event_t* e) {
            auto* self = static_cast<Impl*>(lv_event_get_user_data(e));
            if (self->_preview_awaiting) return;
            const auto index = reinterpret_cast<std::size_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(e)));
            if (!self->_visible_files || index >= self->_visible_files->names.size()) return;
            self->_preview_name = self->_visible_files->names[index];
        }, LV_EVENT_CLICKED, this);
    }
    char label[40];
    std::snprintf(label, sizeof(label), "%u / %u   (%u)", static_cast<unsigned>(_file_page + 1),
                  static_cast<unsigned>(pages), static_cast<unsigned>(total));
    set_text(_file_page_label, label);
    lv_obj_set_state(_file_prev, LV_STATE_DISABLED, _file_page == 0);
    lv_obj_set_state(_file_next, LV_STATE_DISABLED, _file_page + 1 >= pages);
}

void VibescopeView::Impl::update_file_list()
{
    const bool screens = _detail_kind == 9;
    const bool recording = _snapshot.record_requested || _snapshot.recording;
    if (!_preview_name.empty() && !recording &&
        _runtime.request_record_files(RecordFilesAction::Preview, {_preview_name}, screens)) {
        _preview_name.clear();
        _preview_awaiting = true;
    }
    if (_file_scan_pending && !recording && _runtime.request_record_files(RecordFilesAction::Scan, {}, screens)) {
        _file_scan_pending = false;
        render_file_page();
    }
    const bool busy = _runtime.record_files_busy();
    auto result = _runtime.record_files_result();
    if (result && result->screenshots != screens) result.reset();
    if (result && result->action == RecordFilesAction::Preview) {
        if (_preview_awaiting && !busy) {
            _preview_awaiting = false;
            if (result->pixels) show_preview(result);
        }
    } else if (result && result != _visible_files) {
        _visible_files = result;
        render_file_page();
    }
    if (!screens) lv_obj_set_state(_record_duration, LV_STATE_DISABLED, recording || busy);
    lv_obj_set_state(_file_delete, LV_STATE_DISABLED, recording || busy || _checked_files.empty());
    set_text(lv_obj_get_child(_file_delete, 0), _delete_armed ? "确认删除" : "删除");
    const char* status = recording ? "请先结束录制" :
        !_preview_name.empty() ? "正在打开预览" :
        busy ? "正在读取 SD 卡" : _delete_armed ? "再次点击确认删除勾选的文件" :
        result ? result->message.c_str() : "";
    set_text(_file_status, status);
}

void VibescopeView::Impl::show_preview(const std::shared_ptr<const RecordFilesResult>& owner)
{
    close_preview();
    const auto& result = *owner;
    if (!result.pixels || !result.width || !result.height) return;
    _preview_files = owner;
    _preview_draw_started = GetHAL()->millis();
    _preview_draw_pending = true;
    _preview_dsc               = {};
    _preview_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    _preview_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    _preview_dsc.header.w      = static_cast<std::uint16_t>(result.width);
    _preview_dsc.header.h      = static_cast<std::uint16_t>(result.height);
    _preview_dsc.header.stride = static_cast<std::uint16_t>(result.width * sizeof(std::uint16_t));
    _preview_dsc.data_size     = static_cast<std::uint32_t>(result.width) * result.height * sizeof(std::uint16_t);
    _preview_dsc.data          = reinterpret_cast<const std::uint8_t*>(result.pixels.get());
    auto* overlay = _preview = lv_obj_create(_screen->get());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, kWidth, kHeight);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(overlay, [](lv_event_t* e) {
        static_cast<Impl*>(lv_event_get_user_data(e))->close_preview();
    }, LV_EVENT_CLICKED, this);
    // Draw at native size and paint the white frame over the outer pixels.
    auto* frame = panel(overlay, 0, 0, std::min<unsigned>(result.width, kWidth),
                        std::min<unsigned>(result.height, kHeight), kPlot);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(frame);
    auto* image = lv_image_create(frame);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(image, &_preview_dsc);
    lv_obj_center(image);
    lv_obj_set_style_border_width(frame, 5, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_post(frame, true, 0);

    if (!result.preview_name.empty()) {
        auto* caption = text(overlay, file_basename(result.preview_name), 644, 674);
        lv_obj_set_width(caption, 280);
        lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
    }
}

void VibescopeView::Impl::close_preview()
{
    _preview_draw_pending = false;
    if (_preview) {
        lv_obj_delete(_preview);
        _preview = nullptr;
    }
    _preview_files.reset();
}

void VibescopeView::Impl::show_trigger_config()
{
    open_window("触发设置", 3);
    auto* content = _window->get()->get();
    text(content, "加速度阈值 / g", 118, 93);
    auto* input = lv_textarea_create(content);
    position(input, 325, 75, 285, 58);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_accepted_chars(input, "0123456789.");
    lv_textarea_set_max_length(input, 5);
    lv_obj_set_style_text_font(input, &lv_font_montserrat_24, 0);
    lv_obj_set_style_bg_color(input, lv_color_hex(kPlot), 0);
    lv_obj_set_style_text_color(input, lv_color_hex(kText), 0);
    lv_obj_set_style_border_color(input, lv_color_hex(kLine), 0);
    char initial[16];
    std::snprintf(initial, sizeof(initial), "%d.%02d", _trigger_threshold / 100, _trigger_threshold % 100);
    lv_textarea_set_text(input, initial);
    lv_obj_add_event_cb(input, [](lv_event_t* e) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(e));
        auto* field = lv_event_get_current_target_obj(e);
        const char* value = lv_textarea_get_text(field);
        char* end = nullptr;
        const float threshold = std::strtof(value, &end);
        const bool valid = end != value && !*end && threshold >= .05F && threshold <= 10.F;
        if (valid) self->_trigger_threshold = static_cast<int>(std::lround(threshold * 100.F));
        lv_obj_set_style_border_color(field, lv_color_hex(valid ? kLine : kRed), 0);
    }, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_add_event_cb(input, [](lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        self->show_trigger_keypad(lv_event_get_current_target_obj(event));
    }, LV_EVENT_CLICKED, this);
    text(content, "触发位置", 118, 175);
    auto* position_choice = dropdown(content, "0%\n25%\n50%\n75%\n100%", 325, 162, 285,
        [this](unsigned index) { _trigger_position = 25U * index; });
    lv_dropdown_set_selected(position_choice, _trigger_position / 25U);
    _window->open();
}

void VibescopeView::Impl::close_trigger_keypad()
{
    if (!_trigger_keypad) return;
    lv_obj_delete(_trigger_keypad);
    _trigger_keypad = nullptr;
}

void VibescopeView::Impl::show_trigger_keypad(lv_obj_t* input)
{
    if (_trigger_keypad) return;
    _trigger_keypad = panel(_screen->get(), 0, 0, kWidth, kHeight);
    lv_obj_set_style_bg_opa(_trigger_keypad, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(_trigger_keypad, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_trigger_keypad, [](lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        self->close_trigger_keypad();
    }, LV_EVENT_CLICKED, this);

    auto* keypad = lv_keyboard_create(_trigger_keypad);
    lv_obj_set_size(keypad, 440, 240);
    lv_obj_align(keypad, LV_ALIGN_TOP_MID, 0, 326);
    static const char* keys[] = {"1", "2", "3", "\n", "4", "5", "6", "\n",
                                 "7", "8", "9", "\n", ".", "0", LV_SYMBOL_BACKSPACE, ""};
    constexpr auto key_width = static_cast<lv_buttonmatrix_ctrl_t>(1);
    static const lv_buttonmatrix_ctrl_t key_sizes[] = {
        key_width, key_width, key_width, key_width, key_width, key_width,
        key_width, key_width, key_width, key_width, key_width, key_width,
    };
    lv_keyboard_set_map(keypad, LV_KEYBOARD_MODE_NUMBER, keys, key_sizes);
    lv_keyboard_set_mode(keypad, LV_KEYBOARD_MODE_NUMBER);
    const auto* accepted = lv_textarea_get_accepted_chars(input);
    if (accepted && !std::strchr(accepted, '.'))
        lv_buttonmatrix_set_button_ctrl(keypad, 9, LV_BUTTONMATRIX_CTRL_DISABLED);
    lv_keyboard_set_textarea(keypad, input);
    lv_obj_set_style_bg_color(keypad, lv_color_hex(kPanel), 0);
    border(keypad);
    lv_obj_set_style_bg_color(keypad, lv_color_hex(kSoft), LV_PART_ITEMS);
    lv_obj_set_style_text_color(keypad, lv_color_hex(kText), LV_PART_ITEMS);
}

namespace {
constexpr int kPowerRailWidth = ScopeWindow::kDialogWidth * 3 / 4, kPowerRailHeight = 60;
constexpr int kPowerInset = 4, kPowerThumbWidth = 84;
constexpr int kPowerTravel = kPowerRailWidth - kPowerThumbWidth - 2 * kPowerInset;
constexpr int kPowerReleasePercent = 95;
constexpr std::uint32_t kShutdownFadeMs = 650;
}

void VibescopeView::Impl::show_device_settings()
{
    open_window("设备设置", 8);
    _window->can_close = [this] { return !_device_export_started || !_runtime.record_files_busy(); };
    auto* content = _window->get()->get();
    text(content, "日期", 36, 100, kMuted);
    _date_field = numeric_field(content, 104, 84, 170, 8);
    text(content, "时间", 300, 100, kMuted);
    _time_field = numeric_field(content, 368, 84, 170, 6);
    const auto now = std::time(nullptr);
    if (const auto* local = std::localtime(&now)) {
        char buffer[16];
        std::strftime(buffer, sizeof(buffer), "%Y%m%d", local);
        lv_textarea_set_text(_date_field, buffer);
        std::strftime(buffer, sizeof(buffer), "%H%M%S", local);
        lv_textarea_set_text(_time_field, buffer);
    }
    auto* apply = lv_button_create(content);
    surface(apply, kSoft, 5);
    position(apply, 566, 84, 170, 48);
    lv_obj_center(text(apply, "设置时间", 0, 0));
    lv_obj_add_event_cb(apply, [](lv_event_t* event) {
        static_cast<Impl*>(lv_event_get_user_data(event))->apply_device_time();
    }, LV_EVENT_CLICKED, this);

    text(content, "自动休眠", 36, 156, kMuted);
    auto* sleep = dropdown(content, "不休眠\n1 分钟\n5 分钟\n10 分钟", 132, 140, 216,
                           [this](unsigned value) {
        _device_preferences.sleep_index = value;
        device_preferences_changed();
    });
    lv_dropdown_set_selected(sleep, _device_preferences.sleep_index);
    text(content, "休眠关机", 414, 156, kMuted);
    // The display order is independent of the indices saved by older firmware.
    static constexpr std::array<std::uint8_t, 4> power_off_options{0, hal::kSleepPowerOffImmediate, 1, 2};
    auto* sleep_power_off = dropdown(content, "不关机\n直接关机\n1 分钟\n5 分钟", 516, 140, 220,
                                    [this](unsigned value) {
        _device_preferences.sleep_power_off_index = power_off_options[value];
        device_preferences_changed();
    });
    const auto selected_power_off = std::find(power_off_options.begin(), power_off_options.end(),
                                             _device_preferences.sleep_power_off_index);
    lv_dropdown_set_selected(sleep_power_off, selected_power_off - power_off_options.begin());

    auto set_percent = [](lv_obj_t* label, unsigned value) {
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "%u%%", value);
        set_text(label, buffer);
    };
    auto make_slider = [&](int y, unsigned minimum, unsigned value) {
        auto* slider = lv_slider_create(content);
        position(slider, 104, y, 560, 36);
        lv_slider_set_range(slider, minimum, 100);
        lv_slider_set_value(slider, value, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider, lv_color_hex(kSoft), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, lv_color_hex(kAccent), LV_PART_INDICATOR);
        lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_hex(kAccent), LV_PART_KNOB);
        lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);
        lv_obj_set_style_border_color(slider, lv_color_hex(kPanel), LV_PART_KNOB);
        return slider;
    };
    text(content, "音量", 36, 213, kMuted);
    _volume_slider = make_slider(199, 0, _device_preferences.volume);
    _volume_label = text(content, "", 690, 213, kText);
    set_percent(_volume_label, _device_preferences.volume);
    lv_obj_add_event_cb(_volume_slider, [](lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        const auto value = static_cast<unsigned>(lv_slider_get_value(lv_event_get_current_target_obj(event)));
        self->_device_preferences.volume = value;
        GetHAL()->setSpeakerVolume(self->_device_preferences.volume);
        self->device_preferences_changed();
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "%u%%", value);
        set_text(self->_volume_label, buffer);
    }, LV_EVENT_VALUE_CHANGED, this);
    text(content, "亮度", 36, 266, kMuted);
    _brightness_slider = make_slider(252, 1, _device_preferences.brightness);
    _brightness_label = text(content, "", 690, 266, kText);
    set_percent(_brightness_label, _device_preferences.brightness);
    lv_obj_add_event_cb(_brightness_slider, [](lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        const auto value = static_cast<unsigned>(lv_slider_get_value(lv_event_get_current_target_obj(event)));
        self->_device_preferences.brightness = value;
        GetHAL()->setDisplayBrightness(self->_device_preferences.brightness);
        self->device_preferences_changed();
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "%u%%", value);
        set_text(self->_brightness_label, buffer);
    }, LV_EVENT_VALUE_CHANGED, this);

    _device_export = lv_button_create(content);
    surface(_device_export, kSoft, 6);
    constexpr int export_row_top = 309, export_row_height = 48;
    position(_device_export, 356, export_row_top, 190, export_row_height);
    lv_obj_set_style_opa(_device_export, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_center(text(_device_export, "连接电脑", 0, 0));
    text(content, "TF卡", 36, 325, kMuted);
    _device_sd_bar = lv_bar_create(content);
    position(_device_sd_bar, 100, 320, 220, 26);
    lv_bar_set_range(_device_sd_bar, 0, 1000);
    lv_obj_set_style_bg_color(_device_sd_bar, lv_color_hex(kSoft), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_device_sd_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(_device_sd_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_device_sd_bar, lv_color_hex(kAccent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(_device_sd_bar, LV_OPA_30, LV_PART_INDICATOR);
    _device_sd_usage = text(_device_sd_bar, "-- / -- GB", 0, 0, kText);
    lv_obj_center(_device_sd_usage);
    _device_export_status = text(content, "", 564, 325, kMuted);
    lv_obj_set_width(_device_export_status, ScopeWindow::kDialogWidth - 564 - 36);
    lv_label_set_long_mode(_device_export_status, LV_LABEL_LONG_DOT);
    lv_obj_add_event_cb(_device_export, [](lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (self->_runtime.record_export_requested()) self->_runtime.stop_record_export();
        else {
            const auto baseline = self->_runtime.record_files_result();
            if (self->_runtime.request_record_files(RecordFilesAction::Export)) {
                self->_export_baseline = baseline;
                self->_device_export_started = true;
                self->set_acquisition_running(false);
            }
        }
        self->update_device_export();
    }, LV_EVENT_CLICKED, this);

    constexpr int power_rail_top =
        (export_row_top + export_row_height + ScopeWindow::kDialogHeight - kPowerRailHeight) / 2;
    _power_button = panel(content, (ScopeWindow::kDialogWidth - kPowerRailWidth) / 2, power_rail_top,
                          kPowerRailWidth, kPowerRailHeight, kSoft, LV_RADIUS_CIRCLE);
    border(_power_button);
    lv_obj_set_style_opa(_power_button, LV_OPA_50, LV_STATE_DISABLED);
    _power_label = text(_power_button, "滑动关机", 0, 0);
    lv_obj_center(_power_label);
    _power_thumb = lv_button_create(_power_button);
    surface(_power_thumb, kAccent, LV_RADIUS_CIRCLE);
    position(_power_thumb, kPowerInset, kPowerInset, kPowerThumbWidth, kPowerRailHeight - 2 * kPowerInset);
    lv_obj_add_flag(_power_thumb, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_center(text(_power_thumb, LV_SYMBOL_RIGHT, 0, 0, kOnAccent, &lv_font_montserrat_16));
    lv_obj_add_event_cb(_power_thumb, [](lv_event_t* event) {
        const auto code = lv_event_get_code(event);
        if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
            code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->_power_thumb || self->_shutdown_pending ||
            lv_obj_has_state(self->_power_button, LV_STATE_DISABLED)) return;
        auto* input = lv_indev_active();
        if (!input || lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER) return;
        lv_point_t point{};
        lv_indev_get_point(input, &point);
        switch (code) {
        case LV_EVENT_PRESSED:
            self->_power_press_x = point.x;
            break;
        case LV_EVENT_PRESSING: {
            const int travel = std::clamp<int>(point.x - self->_power_press_x, 0, kPowerTravel);
            lv_obj_set_x(self->_power_thumb, kPowerInset + travel);
            break;
        }
        case LV_EVENT_RELEASED: {
            const int travel = std::clamp<int>(point.x - self->_power_press_x, 0, kPowerTravel);
            if (travel * 100 <= kPowerTravel * kPowerReleasePercent) {
                lv_obj_set_x(self->_power_thumb, kPowerInset);
                break;
            }
            self->begin_shutdown();
            if (!self->_shutdown_pending) lv_obj_set_x(self->_power_thumb, kPowerInset);
            break;
        }
        case LV_EVENT_PRESS_LOST:
            lv_obj_set_x(self->_power_thumb, kPowerInset);
            break;
        default:
            break;
        }
    }, LV_EVENT_ALL, this);

    update_device_export();
    _window->open();
}

void VibescopeView::Impl::update_device_export()
{
    const bool busy = _runtime.record_files_busy();
    const bool exporting = _runtime.record_export_requested();
    const bool recording = _snapshot.record_requested || _snapshot.recording;
    const bool active = _device_export_started && busy;
    lv_obj_set_state(_device_export, LV_STATE_DISABLED, _shutdown_pending || recording || (busy && !exporting));
    lv_obj_set_state(_power_button, LV_STATE_DISABLED, active);
    lv_obj_set_state(_power_thumb, LV_STATE_DISABLED, active);
    if (active && !_shutdown_pending) lv_obj_set_x(_power_thumb, kPowerInset);
    set_text(lv_obj_get_child(_device_export, 0), exporting ? "退出导出" : "连接电脑");
    const auto result = _device_export_started && !busy ? _runtime.record_files_result() : nullptr;
    const auto usb = GetHAL()->getComputerUsbState();
    const char* status = exporting ? "USB磁盘模式" :
        active ? "正在退出导出" :
        result && result != _export_baseline && result->action == RecordFilesAction::Export ? result->message.c_str() :
        recording ? "请先结束录制" :
        usb == hal::ConnectionState::Connected ? "已连接" :
        usb == hal::ConnectionState::Connecting ? "正在连接" : "未连接";
    set_text(_device_export_status, status);

    std::uint64_t used = 0, total = 0;
    if (GetHAL()->getSdCardUsage(used, total)) {
        constexpr std::uint64_t bytes_per_gb = 1000000000;
        constexpr std::uint64_t bytes_per_centigb = bytes_per_gb / 100;
        char usage[64];
        std::snprintf(usage, sizeof(usage), "%u.%02u / %u.%02u GB",
                      unsigned(used / bytes_per_gb), unsigned(used % bytes_per_gb / bytes_per_centigb),
                      unsigned(total / bytes_per_gb), unsigned(total % bytes_per_gb / bytes_per_centigb));
        set_text(_device_sd_usage, usage);
        lv_bar_set_value(_device_sd_bar, static_cast<int>(used * 1000 / total), LV_ANIM_OFF);
    } else {
        set_text(_device_sd_usage, "-- / -- GB");
        lv_bar_set_value(_device_sd_bar, 0, LV_ANIM_OFF);
    }
}

void VibescopeView::Impl::apply_device_time()
{
    const auto* date = lv_textarea_get_text(_date_field);
    const auto* time = lv_textarea_get_text(_time_field);
    const auto d = std::strtoul(date, nullptr, 10), t = std::strtoul(time, nullptr, 10);
    std::tm value{};
    value.tm_year = static_cast<int>(d / 10000) - 1900;
    value.tm_mon = static_cast<int>(d / 100 % 100) - 1;
    value.tm_mday = static_cast<int>(d % 100);
    value.tm_hour = static_cast<int>(t / 10000);
    value.tm_min = static_cast<int>(t / 100 % 100);
    value.tm_sec = static_cast<int>(t % 100);
    value.tm_isdst = -1;
    auto normalized = value;
    const auto requested = std::mktime(&normalized);
    if (std::strlen(date) != 8 || std::strlen(time) != 6 || value.tm_year < 100 || value.tm_year > 199 ||
        requested == std::time_t{-1} || normalized.tm_year != value.tm_year ||
        normalized.tm_mon != value.tm_mon || normalized.tm_mday != value.tm_mday ||
        normalized.tm_hour != value.tm_hour || normalized.tm_min != value.tm_min || normalized.tm_sec != value.tm_sec) {
        ui::pop_a_toast("日期或时间无效", ui::toast_type::error, 2000, &vn_font_16, 0);
        return;
    }
#if defined(PLATFORM_BUILD_DESKTOP)
    ui::pop_a_toast("请在 TAB5 上设置时间", ui::toast_type::info, 1800, &vn_font_16, 0);
#else
    GetHAL()->setRtcTime(normalized);
    const bool applied = std::abs(std::difftime(std::time(nullptr), requested)) <= 2;
    ui::pop_a_toast(applied ? "时间已设置" : "时间设置失败",
                   applied ? ui::toast_type::success : ui::toast_type::error, 1800, &vn_font_16, 0);
#endif
}

void VibescopeView::Impl::device_preferences_changed()
{
    _device_preferences_dirty = true;
    _device_preferences_changed = GetHAL()->millis();
    lv_display_trigger_activity(lv_obj_get_display(_screen->get()));
}

bool VibescopeView::Impl::save_device_preferences()
{
    if (_device_preferences_dirty && !GetHAL()->saveDevicePreferences(_device_preferences)) return false;
    _device_preferences_dirty = false;
    return true;
}

void VibescopeView::Impl::service_idle_power()
{
    if (lv_screen_active() != _screen->get()) return;
    auto* display = lv_obj_get_display(_screen->get());
    // Event-driven touch may not report again while a finger is held still.
    for (auto* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input)) {
        if (lv_indev_get_display(input) == display && lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED) {
            lv_display_trigger_activity(display);
            return;
        }
    }
    const auto idle = lv_display_get_inactive_time(display);
    if (_sleep_overlay) {
        const auto delay = hal::kSleepPowerOffDelayMs[_device_preferences.sleep_power_off_index];
        if (delay && idle >= delay) begin_shutdown();
        return;
    }
    const auto delay = hal::kSleepDelayMs[_device_preferences.sleep_index];
    if (!delay || idle < delay || _cal_step || _runtime.record_files_busy() ||
        lv_obj_has_state(_screenshot_button->get(), LV_STATE_CHECKED)) return;
    if (_device_preferences.sleep_power_off_index == hal::kSleepPowerOffImmediate) {
        begin_shutdown();
        return;
    }

    close_window();
    _resume_after_sleep = _snapshot.running;
    _runtime.record(false);
    _record_intent = false;
    set_acquisition_running(false);
    _sleep_overlay = panel(_screen->get(), 0, 0, kWidth, kHeight, kBackground);
    lv_obj_add_flag(_sleep_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(text(_sleep_overlay, "设备休眠", 0, 0, kText, &vn_font_32), LV_ALIGN_CENTER, 0, -32);
    lv_obj_align(text(_sleep_overlay, "点击唤醒", 0, 0, kMuted, &vn_font_32), LV_ALIGN_CENTER, 0, 32);
    lv_obj_add_event_cb(_sleep_overlay, [](lv_event_t* event) {
        static_cast<Impl*>(lv_event_get_user_data(event))->wake_from_sleep();
    }, LV_EVENT_CLICKED, this);
    GetHAL()->setDisplayBrightness(std::clamp<int>(_device_preferences.brightness / 5, 1, 8));
    lv_display_trigger_activity(display);
}

void VibescopeView::Impl::wake_from_sleep()
{
    if (!_sleep_overlay || _shutdown_pending) return;
    lv_obj_delete(_sleep_overlay);
    _sleep_overlay = nullptr;
    GetHAL()->setDisplayBrightness(_device_preferences.brightness);
    lv_display_trigger_activity(lv_obj_get_display(_screen->get()));
    if (_resume_after_sleep) set_acquisition_running(true);
    _resume_after_sleep = false;
}

void VibescopeView::Impl::begin_shutdown()
{
    if (_shutdown_pending) return;
    if (!save_device_preferences()) {
        wake_from_sleep();
        lv_display_trigger_activity(lv_obj_get_display(_screen->get()));
        ui::pop_a_toast("设备参数保存失败", ui::toast_type::error, 2300, &vn_font_16, 0);
        return;
    }
    _runtime.begin_shutdown();
    _shutdown_brightness = GetHAL()->getDisplayBrightness();
    _shutdown_at = GetHAL()->millis();
    _shutdown_pending = true;
    if (_keyboard_connected) GetHAL()->requestKeyboardStatusColor(kKeyboardShutdownColor);
    if (_power_thumb) {
        lv_obj_set_x(_power_thumb, kPowerInset + kPowerTravel);
        set_text(_power_label, "正在关机");
    }
}

void VibescopeView::Impl::service_shutdown()
{
    const auto elapsed = GetHAL()->millis() - _shutdown_at;
    const auto remaining = kShutdownFadeMs - std::min(elapsed, kShutdownFadeMs);
    const auto brightness = _shutdown_brightness * remaining / kShutdownFadeMs;
    if (GetHAL()->getDisplayBrightness() != brightness) GetHAL()->setDisplayBrightness(brightness);
    if (remaining) return;
    const bool ready = _runtime.shutdown_ready();
    if (!ready && elapsed < 5000) return;
#if !defined(PLATFORM_BUILD_DESKTOP)
    if (ready) GetHAL()->powerOff();
#endif
    _runtime.cancel_shutdown();
    LvglLockGuard lock;
    _shutdown_pending = false;
    _resume_after_sleep = false;
    lv_display_trigger_activity(lv_obj_get_display(_screen->get()));
    if (_sleep_overlay) wake_from_sleep();
    else GetHAL()->setDisplayBrightness(_device_preferences.brightness);
    if (_power_thumb) {
        lv_obj_set_x(_power_thumb, kPowerInset);
        set_text(_power_label, "滑动关机");
    }
#if defined(PLATFORM_BUILD_DESKTOP)
    if (ready) {
        ui::pop_a_toast("请在 TAB5 上关机", ui::toast_type::info, 1800, &vn_font_16, 0);
        return;
    }
#endif
    ui::pop_a_toast(ready ? "关机失败" : "等待停止超时", ui::toast_type::error, 2300, &vn_font_16, 0);
}

}  // namespace vibescope_view
