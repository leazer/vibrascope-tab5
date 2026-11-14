/**
 * @file view_keyboard.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-11
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "view_private.h"
#include "../scope_keyboard.h"

namespace vibescope_view {
namespace {
void collect_controls(lv_obj_t* parent, std::vector<lv_obj_t*>& controls)
{
    for (std::uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
        auto* child = lv_obj_get_child(parent, i);
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN) || lv_obj_has_state(child, LV_STATE_DISABLED)) continue;
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE) &&
            (lv_obj_get_event_count(child) || lv_obj_get_class(child) != &lv_obj_class))
            controls.push_back(child);
        collect_controls(child, controls);
    }
}

unsigned navigation_key(unsigned key)
{
    switch (key) {
    case KeyLeft: return LV_KEY_LEFT;
    case KeyRight: return LV_KEY_RIGHT;
    case KeyUp: return LV_KEY_UP;
    case KeyDown: return LV_KEY_DOWN;
    default: return 0;
    }
}

void send_key(lv_obj_t* object, std::uint32_t key)
{
    lv_obj_send_event(object, LV_EVENT_KEY, &key);
}

void click(lv_obj_t* control)
{
    if (control && !lv_obj_has_state(control, LV_STATE_DISABLED))
        lv_obj_send_event(control, LV_EVENT_CLICKED, nullptr);
}
}

void VibescopeView::Impl::update_keyboard(std::uint32_t now)
{
    if (now - _keyboard_tick < 20) return;
    _keyboard_tick = now;
    const auto keyboard = GetHAL()->getKeyboardState();
    const bool connected = keyboard.connected;
    if (connected != _keyboard_connected) {
        _keyboard_connected = connected;
        lv_obj_update_flag(_keyboard_icon, LV_OBJ_FLAG_HIDDEN, !connected);
        if (!connected) focus_keyboard_control(nullptr);
    }
    if (!connected) return;

    if (keyboard.pressedCount()) lv_display_trigger_activity(lv_obj_get_display(_screen->get()));
    hal::KeyboardPress press;
    for (unsigned i = 0; i < 32 && GetHAL()->popKeyboardPress(press); ++i) {
        lv_display_trigger_activity(lv_obj_get_display(_screen->get()));
        if (handle_keyboard_press(press)) GetHAL()->playKeyClick();
    }
    GetHAL()->requestKeyboardStatusColor(keyboard_status_color(
        _shutdown_pending, _sleep_overlay != nullptr, _runtime.record_export_requested(), _cal_step != nullptr,
        _snapshot.record_requested || _snapshot.recording, _snapshot.armed, _snapshot.connected, _snapshot.running));
}

void VibescopeView::Impl::focus_keyboard_control(lv_obj_t* control)
{
    if (_keyboard_focus == control) return;
    if (_keyboard_focus && lv_obj_is_valid(_keyboard_focus)) {
        if (lv_obj_check_type(_keyboard_focus, &lv_dropdown_class) && lv_dropdown_is_open(_keyboard_focus))
            send_key(_keyboard_focus, LV_KEY_ESC);
        lv_obj_remove_state(_keyboard_focus, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    }
    _keyboard_focus = control;
    if (control) {
        lv_obj_set_style_outline_width(control, 2, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_color(control, lv_color_hex(kAccent), LV_STATE_FOCUS_KEY);
        lv_obj_add_state(control, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    }
}

bool VibescopeView::Impl::handle_window_key(unsigned key, std::uint8_t modifiers)
{
    if (_window->getState() == ui::Window::Closing || _window->getState() == ui::Window::Closed) return false;
    auto* focused = _keyboard_focus;
    if (focused && (!lv_obj_is_valid(focused) || lv_obj_has_flag(focused, LV_OBJ_FLAG_HIDDEN) ||
                    lv_obj_has_state(focused, LV_STATE_DISABLED))) {
        focus_keyboard_control(nullptr);
        focused = nullptr;
    }
    if (key == KeyEscape) {
        if (_trigger_keypad) close_trigger_keypad();
        else if (focused && lv_obj_check_type(focused, &lv_dropdown_class) && lv_dropdown_is_open(focused))
            send_key(focused, LV_KEY_ESC);
        else _window->close();
        return true;
    }
    if (modifiers & (hal::KeyCtrl | hal::KeyAlt)) return false;

    if (_trigger_keypad) {
        auto* target = lv_keyboard_get_textarea(lv_obj_get_child(_trigger_keypad, 0));
        if (key == KeyEnter || key == KeyTab) {
            close_trigger_keypad();
            focus_keyboard_control(target);
            if (key == KeyEnter) return true;
            focused = target;
        } else if (target && ((key >= '0' && key <= '9') || key == '.')) {
            lv_textarea_add_char(target, key);
            return true;
        } else if (target && (key == KeyBackspace || key == KeyDelete)) {
            if (key == KeyBackspace) lv_textarea_delete_char(target);
            else lv_textarea_delete_char_forward(target);
            return true;
        } else return false;
    }

    if (key == KeyTab || !focused) {
        if (key != KeyTab && !navigation_key(key)) return false;
        std::vector<lv_obj_t*> controls;
        collect_controls(_window->get()->get(), controls);
        if (controls.empty()) return false;
        const auto current = std::find(controls.begin(), controls.end(), focused);
        const bool back = (modifiers & hal::KeyShift) || key == KeyUp || key == KeyLeft;
        const auto index = current == controls.end() ? (back ? controls.size() - 1 : 0) :
            (current - controls.begin() + (back ? controls.size() - 1 : 1)) % controls.size();
        focus_keyboard_control(controls[index]);
        return true;
    }

    if (lv_obj_check_type(focused, &lv_textarea_class)) {
        if ((key >= '0' && key <= '9') || key == '.') lv_textarea_add_char(focused, key);
        else if (key == KeyBackspace) lv_textarea_delete_char(focused);
        else if (key == KeyDelete) lv_textarea_delete_char_forward(focused);
        else if (key == KeyEnter) return handle_window_key(KeyTab, 0);
        else if (const auto arrow = navigation_key(key)) send_key(focused, arrow);
        else return false;
        return true;
    }
    if (key == KeyEnter || key == ' ') {
        if (focused == _power_thumb) {
            if (key != KeyEnter) return false;
            begin_shutdown();
        } else if (lv_obj_check_type(focused, &lv_dropdown_class)) send_key(focused, LV_KEY_ENTER);
        else if (lv_obj_check_type(focused, &lv_checkbox_class)) lv_obj_send_event(focused, LV_EVENT_RELEASED, nullptr);
        else click(focused);
        return true;
    }
    if (const auto arrow = navigation_key(key)) {
        if (lv_obj_check_type(focused, &lv_dropdown_class) || lv_obj_check_type(focused, &lv_slider_class))
            send_key(focused, arrow);
        else
            return handle_window_key(KeyTab, key == KeyLeft || key == KeyUp ? hal::KeyShift : 0);
        return true;
    }
    if (key == KeyDelete && (_detail_kind == 5 || _detail_kind == 9)) {
        click(_file_delete);
        return true;
    }
    return false;
}

bool VibescopeView::Impl::handle_keyboard_press(const hal::KeyboardPress& press)
{
    if (_shutdown_pending || lv_screen_active() != _screen->get()) return false;
    const auto key = physical_key(press);
    if (key == KeyNone) return false;
    if (_sleep_overlay) {
        wake_from_sleep();
        return true;
    }
    if (_preview) {
        if (key != KeyEscape && key != KeyEnter && key != ' ') return false;
        close_preview();
        return true;
    }
    if (_window) return handle_window_key(key, press.modifiers);

    const bool ctrl = press.modifiers & hal::KeyCtrl;
    const bool shift = press.modifiers & hal::KeyShift;
    if (press.modifiers & hal::KeyAlt) return false;
    if (ctrl) {
        if (!shift && key >= '1' && key <= '3') {
            if (_compass_mode) return true;
            lv_dropdown_set_selected(_layout_dropdown, key - '1');
            lv_obj_send_event(_layout_dropdown, LV_EVENT_VALUE_CHANGED, nullptr);
            return true;
        }
        if (shift) {
            switch (key) {
            case 'r': show_record_config(); break;
            case 's': show_screenshot_files(); break;
            case 't': show_trigger_config(); break;
            case 'u': show_cursor_config(); break;
            case 'm': show_measurement_settings(); show_calibration(); break;
            case 'q': show_device_settings(); focus_keyboard_control(_power_thumb); break;
            default: return false;
            }
        } else {
            switch (key) {
            case 'r': toggle_recording(); break;
            case 's': capture_screenshot(); break;
            case 't': lv_obj_send_event(_trigger_button->get(), LV_EVENT_SHORT_CLICKED, nullptr); break;
            case 'u':
                if (_compass_mode || _configs[_selected].kind != PlotKind::Waterfall)
                    lv_obj_send_event(_cursor_button->get(), LV_EVENT_SHORT_CLICKED, nullptr);
                break;
            case 'w':
                if (_compass_mode) show_rms_trend_config();
                else show_waveform_config();
                break;
            case 'm': show_measurement_settings(); break;
            case 'd': show_device_settings(); break;
            case 'e': show_device_settings(); focus_keyboard_control(_device_export); break;
            case 'g': toggle_compass(); break;
            case 'i': show_about(); break;
            case 'h': show_keyboard_help(); break;
            case 'b': toggle_sidebar(); break;
            default: return false;
            }
        }
        return true;
    }

    if (!shift) {
        if (key == ' ') click(_run->get());
        else if (key >= '1' && key <= '3') {
            const unsigned index = key - '1';
            if (!_compass_mode && index < _layout_count) select(index);
        } else if (key == KeyUp || key == KeyDown) {
            if (!_compass_mode) select((_selected + (key == KeyUp ? _layout_count - 1 : 1)) % _layout_count);
        } else if (key == '+' || key == '-' || key == '0') {
            navigate(_selected, key == '+' ? 0 : key == '-' ? 1 : 2);
        } else if (key != KeyLeft && key != KeyRight) return false;
        if (key != KeyLeft && key != KeyRight) return true;
    }
    if (key != KeyLeft && key != KeyRight) return false;
    if (_compass_mode) {
        auto& config = _rms_cursor;
        if (!config.cursor || (shift && !config.dual_cursor)) return false;
        auto& fraction = shift ? config.cursor_b_fraction : config.cursor_fraction;
        const int direction = key == KeyLeft ? -1 : 1;
        const auto span = kRmsTrendWindowsSeconds[_rms_trend_window];
        fraction = std::clamp((std::round(fraction * span) + direction) / span, 0.F, 1.F);
        render_rms_cursor();
        return true;
    }
    auto& config = _configs[_selected];
    auto& plot = _plots[_selected];
    if (config.kind == PlotKind::Waterfall || !plot.rendered_valid || !plot.rendered.valid) return false;
    const int direction = key == KeyLeft ? -1 : 1;
    if (config.cursor) {
        if (shift && !config.dual_cursor) return false;
        auto& fraction = shift ? config.cursor_b_fraction : config.cursor_fraction;
        fraction = std::clamp(fraction + direction * .01F, 0.F, 1.F);
        render_cursor(_selected);
    } else if (!shift && !_snapshot.running) {
        const bool spectrum = config.kind == PlotKind::Spectrum;
        const auto first = spectrum ? plot.frequency_band.first : 0;
        const auto count = spectrum ? plot.frequency_band.count : plot_data_count(config);
        SampleViewport viewport{std::max(first, plot.viewport.first) - first, plot.viewport.count};
        viewport.pan(count, direction * static_cast<std::int64_t>(std::max<std::size_t>(1, viewport.count / 20)));
        plot.viewport = {first + viewport.first, viewport.count};
        render_plot(_selected);
    } else return false;
    return true;
}

void VibescopeView::Impl::show_keyboard_help()
{
    open_window("键盘快捷键", 10);
    auto* content = _window->get()->get();
    text(content, "主界面", 42, 67, kAccent);
    text(content, "Space   运行或停止\n"
                  "Ctrl+R / Ctrl+Aa+R   录制 / 录制设置\n"
                  "Ctrl+S / Ctrl+Aa+S   截屏 / 截屏管理\n"
                  "Ctrl+T / Ctrl+Aa+T   触发 / 触发设置\n"
                  "Ctrl+U / Ctrl+Aa+U   游标 / 游标设置\n"
                  "Ctrl+W   波形设置\n"
                  "Ctrl+M / Ctrl+Aa+M   探头设置 / 标定\n"
                  "Ctrl+D / Ctrl+E   设备设置 / 连接电脑\n"
                  "Ctrl+G / Ctrl+I   振动强度 / 关于\n"
                  "Ctrl+B / Ctrl+H   侧栏 / 快捷键", 42, 104);
    text(content, "图表与弹窗", 464, 67, kAccent);
    text(content, "1–3   选择图表\n"
                  "Ctrl+1–3   单图 / 双图 / 三分图\n"
                  "上 / 下   切换图表\n"
                  "+ / - / 0   放大 / 缩小 / 适应\n"
                  "左 / 右   移动游标或平移暂停画面\n"
                  "Aa+左 / 右   移动第二根游标\n"
                  "Tab / Aa+Tab   下一个 / 上一个控件\n"
                  "方向键 / Enter   调整 / 确认\n"
                  "Esc / Del   返回 / 删除所选文件\n"
                  "Ctrl+Aa+Q   关机确认，再按 Enter", 464, 104);
    text(content, "灯光  蓝 待机 / 绿 采集 / 琥珀 触发 / 红 录制 / 紫 标定 / 青 导出 / 暗蓝 休眠",
         42, 372, kMuted);
    text(content, "Aa 是键盘上的 Shift。数字键可直接填写频段和时间。", 42, 425, kMuted);
    _window->open();
}
}  // namespace vibescope_view
