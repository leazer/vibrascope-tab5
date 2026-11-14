/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal/hal_esp32.h"
#include "panic_screen.h"
extern "C" {
#include "utils/rx8130/rx8130.h"
}
#include <mooncake_log.h>
#include <esp_timer.h>
#include <esp_vfs_fat.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <bsp/m5stack_tab5.h>
#include <lv_demos.h>
#include <driver/gpio.h>

extern esp_lcd_touch_handle_t _lcd_touch_handle;

static const std::string _tag = "hal";

static void lvgl_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    if (_lcd_touch_handle == NULL) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    uint16_t touch_x[1];
    uint16_t touch_y[1];
    uint16_t touch_strength[1];
    uint8_t touch_cnt = 0;

    esp_lcd_touch_read_data(_lcd_touch_handle);
    bool touchpad_pressed =
        esp_lcd_touch_get_coordinates(_lcd_touch_handle, touch_x, touch_y, touch_strength, &touch_cnt, 1);
    // mclog::tagInfo(_tag, "touchpad pressed: {}", touchpad_pressed);

    if (!touchpad_pressed) {
        data->state = LV_INDEV_STATE_REL;
    } else {
        data->state   = LV_INDEV_STATE_PR;
        data->point.x = touch_x[0];
        data->point.y = touch_y[0];
    }
}

// The indev dispatches this while the actual pressed control is available,
// including touch and the USB mouse.
static void key_click_event(lv_event_t* event)
{
    auto* object = static_cast<lv_obj_t*>(lv_event_get_param(event));
    if (object && lv_obj_has_flag(object, LV_OBJ_FLAG_CLICKABLE) &&
        !lv_obj_has_state(object, LV_STATE_DISABLED) &&
        (lv_obj_get_event_count(object) || lv_obj_get_class(object) != &lv_obj_class))
        GetHAL()->playKeyClick();
}

namespace {
constexpr char kDeviceNvsNamespace[] = "vibra_device";
constexpr char kDeviceNvsKey[] = "settings";
}

bool HalEsp32::saveDevicePreferences(const hal::DevicePreferences& preferences)
{
    if (!_device_nvs_ready) return false;
    nvs_handle_t handle;
    if (nvs_open(kDeviceNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    const auto written = nvs_set_u32(handle, kDeviceNvsKey, preferences.encode());
    const bool saved = written == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    if (saved) _device_preferences = preferences;
    return saved;
}

void HalEsp32::init()
{
    // Keep the panel dark throughout peripheral and LCD initialization. Set
    // the output latch before enabling the pin, until LEDC takes ownership.
    ESP_ERROR_CHECK(gpio_set_level(BSP_LCD_BACKLIGHT, 0));
    ESP_ERROR_CHECK(gpio_set_direction(BSP_LCD_BACKLIGHT, GPIO_MODE_OUTPUT));
    mclog::tagInfo(_tag, "init");

    _device_nvs_ready = nvs_flash_init() == ESP_OK;
    nvs_handle_t preferences;
    if (_device_nvs_ready && nvs_open(kDeviceNvsNamespace, NVS_READONLY, &preferences) == ESP_OK) {
        std::uint32_t word;
        if (nvs_get_u32(preferences, kDeviceNvsKey, &word) == ESP_OK)
            _device_preferences = hal::DevicePreferences::decode(word).value_or(hal::DevicePreferences{});
        nvs_close(preferences);
    }
    _current_lcd_brightness = _device_preferences.brightness;
    setSpeakerVolume(_device_preferences.volume);

    mclog::tagInfo(_tag, "camera init");
    bsp_cam_osc_init();

    mclog::tagInfo(_tag, "i2c init");
    bsp_i2c_init();

    mclog::tagInfo(_tag, "io expander init");
    i2c_master_bus_handle_t i2c_bus_handle = bsp_i2c_get_handle();
    bsp_io_expander_pi4ioe_init(i2c_bus_handle);

    setChargeQcEnable(true);
    delay(50);
    // Start with the charger off so the power monitor can detect the pack voltage.
    setChargeEnable(false);

    mclog::tagInfo(_tag, "i2c scan");
    bsp_i2c_scan();

    mclog::tagInfo(_tag, "codec init");
    delay(200);
    bsp_codec_init();

    mclog::tagInfo(_tag, "imu init");
    imu_init();

    mclog::tagInfo(_tag, "ina226 init");
    ina226.begin(i2c_bus_handle, 0x41);
    ina226.configure(INA226_AVERAGES_16, INA226_BUS_CONV_TIME_1100US, INA226_SHUNT_CONV_TIME_1100US,
                     INA226_MODE_SHUNT_BUS_CONT);
    ina226.calibrate(0.005, 8.192);
    mclog::tagInfo(_tag, "bus voltage: {}", ina226.readBusVoltage());

    mclog::tagInfo(_tag, "rx8130 init");
    rx8130.begin(i2c_bus_handle, 0x32);
    rx8130.initBat();
    clearRtcIrq();
    update_system_time();

    mclog::tagInfo(_tag, "display init");
    bsp_reset_tp();
    bsp_display_cfg_t cfg = {.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
                             .buffer_size   = BSP_LCD_H_RES * BSP_LCD_V_RES,
                             .double_buffer = true,
                             .flags         = {
#if CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
                                 .buff_dma = false,
#else
                                 .buff_dma = true,
#endif
                                 .buff_spiram = true,
                                 .sw_rotate   = true,
                             }};
    lvDisp = bsp_display_start_with_config(&cfg);
    // The default LVGL screen is white. Submit the complete startup background
    // while the backlight is off, rather than exposing it before the app opens.
    bsp_display_lock(0);
    lv_display_set_rotation(lvDisp, LV_DISPLAY_ROTATION_90);
    std::size_t framebuffer_bytes = 0;
    const auto* framebuffer = lvgl_port_get_dpi_framebuffer(lvDisp, &framebuffer_bytes);
    tab5_register_panic_framebuffer(framebuffer, framebuffer_bytes);
    lv_obj_t* startup_screen = lv_display_get_screen_active(lvDisp);
    lv_obj_set_style_bg_color(startup_screen, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(startup_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_invalidate(startup_screen);
    lv_refr_now(lvDisp);
    // Flush completes the framebuffer write; let the continuously scanning
    // panel consume it before lighting it (at least two 60 Hz scans).
    delay(40);
    ESP_ERROR_CHECK(bsp_display_brightness_set(_current_lcd_brightness));
    mclog::tagInfo(_tag, "startup background flushed before backlight on");
    bsp_display_unlock();

    // // Touchpad lvgl indev
    // mclog::tagInfo(_tag, "create lvgl touchpad indev");
    // lvTouchpad = lv_indev_create();
    // lv_indev_set_type(lvTouchpad, LV_INDEV_TYPE_POINTER);
    // lv_indev_set_read_cb(lvTouchpad, lvgl_read_cb);
    // lv_indev_set_display(lvTouchpad, lvDisp);

    mclog::tagInfo(_tag, "usb host init");
    bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_USB_DEV, true);

    mclog::tagInfo(_tag, "hid init");
    hid_init();
    bsp_display_lock(0);
    for (auto* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
        lv_indev_add_event_cb(input, key_click_event, LV_EVENT_PRESSED, nullptr);
    bsp_display_unlock();

    mclog::tagInfo(_tag, "probe cdc init");
    probe_cdc_init();

    mclog::tagInfo(_tag, "set gpio output capability");
    set_gpio_output_capability();

    mclog::tagInfo(_tag, "tab5 keyboard init");
    keyboard_init();
    status_monitor_init();

}

static const gpio_num_t _driver_gpios[] = {
    // EXT I2C
    GPIO_NUM_0,
    GPIO_NUM_1,
    // esp-hosted esp32c6
    GPIO_NUM_8,
    GPIO_NUM_9,
    GPIO_NUM_10,
    GPIO_NUM_11,
    GPIO_NUM_12,
    GPIO_NUM_13,
    GPIO_NUM_15,
    // Display
    GPIO_NUM_22,
    GPIO_NUM_23,
    // Audio
    GPIO_NUM_26,
    GPIO_NUM_27,
    GPIO_NUM_28,
    GPIO_NUM_29,
    GPIO_NUM_30,
    // SYS I2C
    GPIO_NUM_31,
    GPIO_NUM_32,
    // uSD card
    GPIO_NUM_39,
    GPIO_NUM_40,
    GPIO_NUM_41,
    GPIO_NUM_42,
    GPIO_NUM_43,
    GPIO_NUM_44,
};

void HalEsp32::set_gpio_output_capability()
{
    // gpio_set_drive_capability((gpio_num_t)48, GPIO_DRIVE_CAP_0);
    for (int i = 0; i < sizeof(_driver_gpios) / sizeof(_driver_gpios[0]); i++) {
        gpio_num_t gpio = _driver_gpios[i];
        esp_err_t ret   = gpio_set_drive_capability(gpio, GPIO_DRIVE_CAP_0);
        if (ret == ESP_OK) {
            printf("GPIO %d drive capability set to GPIO_DRIVE_CAP_0\n", gpio);
        } else {
            printf("Failed to set GPIO %d drive capability: %s\n", gpio, esp_err_to_name(ret));
        }
    }
}

/* -------------------------------------------------------------------------- */
/*                                   System                                   */
/* -------------------------------------------------------------------------- */
#include <driver/temperature_sensor.h>
static temperature_sensor_handle_t _temp_sensor = nullptr;

void HalEsp32::delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint32_t HalEsp32::millis()
{
    return esp_timer_get_time() / 1000;
}

int HalEsp32::getCpuTemp()
{
    if (_temp_sensor == nullptr) {
        temperature_sensor_config_t temp_sensor_config = {
            .range_min = 20,
            .range_max = 100,
        };
        temperature_sensor_install(&temp_sensor_config, &_temp_sensor);
        temperature_sensor_enable(_temp_sensor);
    }

    float temp = 0;
    temperature_sensor_get_celsius(_temp_sensor, &temp);

    return temp;
}

/* -------------------------------------------------------------------------- */
/*                                   Display                                  */
/* -------------------------------------------------------------------------- */
void HalEsp32::setDisplayBrightness(uint8_t brightness)
{
    _current_lcd_brightness = std::clamp((int)brightness, 0, 100);
    mclog::tagInfo("hal", "set display brightness: {}%", _current_lcd_brightness);
    bsp_display_brightness_set(_current_lcd_brightness);
}

uint8_t HalEsp32::getDisplayBrightness()
{
    return _current_lcd_brightness;
}

std::string HalEsp32::getDisplayPanelIc()
{
    return std::string(bsp_display_get_panel_ic());
}

void HalEsp32::lvglLock()
{
    lvgl_port_lock(0);
}

void HalEsp32::lvglUnlock()
{
    lvgl_port_unlock();
}

/* -------------------------------------------------------------------------- */
/*                                     RTC                                    */
/* -------------------------------------------------------------------------- */
void HalEsp32::clearRtcIrq()
{
    mclog::tagInfo(_tag, "clear rtc irq");
    rx8130.clearIrqFlags();
    rx8130.disableIrq();
}

void HalEsp32::setRtcTime(tm time)
{
    mclog::tagInfo(_tag, "set rtc time to {}/{}/{} {:02d}:{:02d}:{:02d}", time.tm_year + 1900, time.tm_mon + 1,
                   time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);
    rx8130.setTime(&time);
    delay(50);

    update_system_time();
}

void HalEsp32::update_system_time()
{
    mclog::tagInfo(_tag, "update system time");
    struct tm time;
    rx8130.getTime(&time);
    mclog::tagInfo(_tag, "sync to rtc time: {}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}", time.tm_year + 1900,
                   time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);
    struct timeval now;
    now.tv_sec  = mktime(&time);
    now.tv_usec = 0;
    settimeofday(&now, NULL);
}

/* -------------------------------------------------------------------------- */
/*                                   SD Card                                  */
/* -------------------------------------------------------------------------- */
#include <dirent.h>
#include <sys/types.h>

bool HalEsp32::isSdCardMounted()
{
    return _sd_card_mounted.load();
}

hal::ConnectionState HalEsp32::getSdCardState() { return _sd_state.load(); }

bool HalEsp32::getSdCardUsage(std::uint64_t& used, std::uint64_t& total)
{
    std::lock_guard<std::mutex> lock(_sd_usage_mutex);
    used = _sd_used_bytes;
    total = _sd_total_bytes;
    return total != 0;
}

bool HalEsp32::beginSdRecording()
{
    std::lock_guard<std::mutex> lock(_sd_mutex);
    if (!_sd_card_mounted || _sd_recording) return false;
    _sd_recording = true;
    return true;
}

void HalEsp32::endSdRecording(bool success)
{
    std::lock_guard<std::mutex> lock(_sd_mutex);
    _sd_recording = false;
    _sd_state = success ? hal::ConnectionState::Connected : hal::ConnectionState::Fault;
}

void HalEsp32::refresh_sd_card()
{
    std::lock_guard<std::mutex> lock(_sd_mutex);
    if (_sd_recording) return; // Never probe/unmount media owned by an open recording.
    static char mount_point[] = "/sd";
    const auto refresh_usage = [&] {
        std::uint64_t total = 0, free = 0;
        const bool valid = _sd_card_mounted &&
                           esp_vfs_fat_info(mount_point, &total, &free) == ESP_OK && free <= total;
        std::lock_guard<std::mutex> usage_lock(_sd_usage_mutex);
        _sd_total_bytes = valid ? total : 0;
        _sd_used_bytes = valid ? total - free : 0;
    };
    if (_sd_card_mounted) {
        if (bsp_sdcard_status() == ESP_OK) {
            _sd_state = hal::ConnectionState::Connected;
            refresh_usage();
            return;
        }
        _sd_card_mounted = false;
        _sd_state = hal::ConnectionState::Off;
        bsp_sdcard_deinit(mount_point);
        refresh_usage();
        mclog::tagWarn(_tag, "SD card unavailable; filesystem unmounted");
        return;
    }
    _sd_state = hal::ConnectionState::Connecting;
    const auto result = bsp_sdcard_init(mount_point, 8);
    _sd_card_mounted = result == ESP_OK;
    refresh_usage();
    _sd_state = result == ESP_OK ? hal::ConnectionState::Connected :
        result == ESP_FAIL ? hal::ConnectionState::Fault : hal::ConnectionState::Off;
    mclog::tagInfo(_tag, "SD mount: {}", esp_err_to_name(result));
}

std::vector<hal::HalBase::FileEntry_t> HalEsp32::scanSdCard(const std::string& dirPath)
{
    std::vector<hal::HalBase::FileEntry_t> file_entries;
    std::lock_guard<std::mutex> lock(_sd_mutex);
    if (!_sd_card_mounted || _sd_recording) return file_entries;

    std::string target_path = "/sd/" + dirPath;

    DIR* dir = opendir(target_path.c_str());
    if (dir == nullptr) {
        mclog::error("failed to open directory: {}", target_path);
        return file_entries;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (std::string(entry->d_name) == "." || std::string(entry->d_name) == "..") {
            continue;
        }

        hal::HalBase::FileEntry_t file_entry;
        file_entry.name  = entry->d_name;
        file_entry.isDir = (entry->d_type == DT_DIR);
        file_entries.push_back(file_entry);
    }

    closedir(dir);

    return file_entries;
}

/* -------------------------------------------------------------------------- */
/*                                  Interface                                 */
/* -------------------------------------------------------------------------- */
bool HalEsp32::usbCDetect()
{
    return bsp_usb_c_detect();
    // return false;
}

bool HalEsp32::headPhoneDetect()
{
    return bsp_headphone_detect();
}

std::vector<uint8_t> HalEsp32::i2cScan(bool isInternal)
{
    i2c_master_bus_handle_t i2c_bus_handle;
    std::vector<uint8_t> addrs;

    if (isInternal) {
        i2c_bus_handle = bsp_i2c_get_handle();
    } else {
        if (_keyboard_i2c_reserved) {
            mclog::tagWarn(_tag, "Grove I2C unavailable: I2C1 is reserved for the Tab5 keyboard on GPIO0/1");
            return addrs;
        }
        i2c_bus_handle = bsp_ext_i2c_get_handle();
    }

    esp_err_t ret;
    uint8_t address;

    for (int i = 16; i < 128; i += 16) {
        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            address = i + j;
            ret     = i2c_master_probe(i2c_bus_handle, address, 50);
            if (ret == ESP_OK) {
                addrs.push_back(address);
            }
        }
    }

    return addrs;
}

void HalEsp32::initPortAI2c()
{
    if (_keyboard_i2c_reserved) {
        mclog::tagWarn(_tag, "Grove I2C cannot share I2C1 with the Tab5 keyboard");
        return;
    }
    mclog::tagInfo(_tag, "init port a i2c");
    bsp_ext_i2c_init();
}

void HalEsp32::deinitPortAI2c()
{
    if (_keyboard_i2c_reserved) return;
    mclog::tagInfo(_tag, "deinit port a i2c");
    bsp_ext_i2c_deinit();
}

void HalEsp32::gpioInitOutput(uint8_t pin)
{
    gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLUP_ONLY);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
}

void HalEsp32::gpioSetLevel(uint8_t pin, bool level)
{
    gpio_set_level((gpio_num_t)pin, level);
}

void HalEsp32::gpioReset(uint8_t pin)
{
    gpio_set_level((gpio_num_t)pin, false);
}
