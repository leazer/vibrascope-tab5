/**
 * @file hal_keyboard.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "../hal_esp32.h"
#include <m5_tab5_keyboard.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>

namespace {
constexpr char kTag[] = "tab5_keyboard";
constexpr std::uint32_t kNoColor = 0xffffffff;
struct KeyboardService {
    m5::M5Tab5Keyboard driver;
    i2c_master_bus_handle_t bus{};
    std::mutex control_mutex, state_mutex;
    hal::KeyboardState state;
    hal::KeyboardPressQueue presses;
    std::atomic<std::uint32_t> requested_color{kNoColor};
};
KeyboardService& keyboard()
{
    static KeyboardService service;
    return service;
}

void on_key(m5_tab5_key_event_t event, void*)
{
    if (event.type != M5_TAB5_KB_MODE_NORMAL) return;
    auto& service = keyboard();
    std::lock_guard<std::mutex> lock(service.state_mutex);
    if (event.row >= 5 || event.col >= 14) return;
    const bool was_pressed = service.state.keys[event.row * 14 + event.col];
    if (service.state.apply(event.row, event.col, event.pressed) && event.pressed && !was_pressed)
        service.presses.push({event.row, event.col, service.state.modifiers()});
}

hal::KeyboardState state_snapshot()
{
    auto& service = keyboard();
    std::lock_guard<std::mutex> lock(service.state_mutex);
    return service.state;
}

// Caller owns control_mutex. No state lock is held during driver operations:
// the IRQ worker invokes on_key while it owns the driver's recursive mutex.
bool connect_keyboard()
{
    auto& service = keyboard();
    if (i2c_master_probe(service.bus, M5_TAB5_KB_DEFAULT_ADDR, 20) != ESP_OK) return false;
    auto error = service.driver.begin(service.bus, M5_TAB5_KB_DEFAULT_ADDR, M5_TAB5_KB_I2C_FREQ_400K,
                                      M5_TAB5_KB_DEFAULT_INT, M5_TAB5_KB_INT_MODE_DISABLED);
    uint8_t version = 0, brightness = 0;
    m5_tab5_kb_rgb_mode_t rgb_mode = M5_TAB5_KB_RGB_MODE_BINDING;
    if (error == M5_TAB5_KB_OK) error = service.driver.getVersion(&version);
    if (error == M5_TAB5_KB_OK) error = service.driver.enableNormalMode(on_key, nullptr);
    if (error == M5_TAB5_KB_OK) error = service.driver.setInterruptConfig(0x01);
    if (error == M5_TAB5_KB_OK) {
        service.driver.getBrightness(&brightness);
        service.driver.getRGBMode(&rgb_mode);
        {
            std::lock_guard<std::mutex> lock(service.state_mutex);
            service.state.keys.fill(false);
            service.presses.clear();
            service.state.connected = true;
            service.state.firmwareVersion = version;
            service.state.brightness = brightness;
            service.state.rgbMode = static_cast<uint8_t>(rgb_mode);
        }
        error = service.driver.setInterruptMode(M5_TAB5_KB_INT_MODE_HARDWARE);
    }
    if (error != M5_TAB5_KB_OK) {
        service.driver.end();
        std::lock_guard<std::mutex> lock(service.state_mutex);
        service.state.disconnect();
        ESP_LOGW(kTag, "initialization failed (%d), retrying in background", static_cast<int>(error));
        return false;
    }
    ESP_LOGI(kTag, "connected addr=0x6d fw=0x%02x SDA=0 SCL=1 INT=50 mode=normal brightness=%u rgb_mode=%u",
             static_cast<unsigned>(version), static_cast<unsigned>(brightness), static_cast<unsigned>(rgb_mode));
    return true;
}

void keyboard_monitor(void*)
{
    auto& service = keyboard();
    unsigned failures = 0;
    std::uint32_t applied_color = kNoColor;
    std::uint32_t reported_events = 0;
    unsigned polls = 0;
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(service.control_mutex);
            if (!state_snapshot().connected) {
                connect_keyboard();
                applied_color = kNoColor;
                failures = 0;
            } else {
                uint8_t version = 0;
                if (service.driver.getVersion(&version) == M5_TAB5_KB_OK) {
                    failures = 0;
                    const auto color = service.requested_color.load();
                    if (color != kNoColor && color != applied_color) {
                        const bool custom = state_snapshot().rgbMode == M5_TAB5_KB_RGB_MODE_CUSTOM ||
                            service.driver.setRGBMode(M5_TAB5_KB_RGB_MODE_CUSTOM) == M5_TAB5_KB_OK;
                        if (custom) {
                            {
                                std::lock_guard<std::mutex> state_lock(service.state_mutex);
                                service.state.rgbMode = M5_TAB5_KB_RGB_MODE_CUSTOM;
                            }
                            if (service.driver.setBothRGB((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff) ==
                                M5_TAB5_KB_OK) applied_color = color;
                        }
                    }
                } else if (++failures >= 2) {
                    service.driver.end();
                    {
                        std::lock_guard<std::mutex> state_lock(service.state_mutex);
                        service.state.disconnect();
                        service.presses.clear();
                    }
                    ESP_LOGW(kTag, "disconnected; released all held keys");
                }
            }
        }
        const auto state = state_snapshot();
        if (++polls % 4 == 0 && state.events != reported_events) {
            reported_events = state.events;
            ESP_LOGI(kTag, "events=%lu last=(row=%u,col=%u,%s) held=%u",
                     static_cast<unsigned long>(state.events), static_cast<unsigned>(state.lastRow),
                     static_cast<unsigned>(state.lastColumn), state.lastPressed ? "down" : "up", state.pressedCount());
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
}  // namespace

void HalEsp32::keyboard_init()
{
    auto& service = keyboard();
    m5::M5Tab5Keyboard::setLogLevel(M5_TAB5_KB_LOG_LEVEL_WARN);  // no per-key UART traffic in the IRQ worker
    i2c_master_bus_config_t config{};
    config.i2c_port = I2C_NUM_1;
    config.sda_io_num = GPIO_NUM_0;
    config.scl_io_num = GPIO_NUM_1;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;
    const auto error = i2c_new_master_bus(&config, &service.bus);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "I2C1 unavailable (%s); keyboard disabled", esp_err_to_name(error));
        return;
    }
    _keyboard_i2c_reserved = true;
    if (xTaskCreate(keyboard_monitor, "keyboard_monitor", 6144, nullptr, 3, nullptr) != pdPASS) {
        i2c_del_master_bus(service.bus);
        service.bus = nullptr;
        _keyboard_i2c_reserved = false;
        ESP_LOGE(kTag, "monitor task allocation failed");
        return;
    }
    ESP_LOGI(kTag, "I2C1 GPIO0/1 reserved; hardware IRQ and background discovery enabled");
}

hal::KeyboardState HalEsp32::getKeyboardState()
{
    return state_snapshot();
}

bool HalEsp32::popKeyboardPress(hal::KeyboardPress& press)
{
    auto& service = keyboard();
    std::lock_guard<std::mutex> lock(service.state_mutex);
    return service.presses.pop(press);
}

void HalEsp32::requestKeyboardStatusColor(std::uint32_t rgb)
{
    keyboard().requested_color.store(rgb);
}

bool HalEsp32::setKeyboardRgb(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index > 1) return false;
    auto& service = keyboard();
    std::lock_guard<std::mutex> lock(service.control_mutex);
    return state_snapshot().connected && service.driver.setRGB(index, r, g, b) == M5_TAB5_KB_OK;
}

bool HalEsp32::setKeyboardRgbMode(bool custom)
{
    auto& service = keyboard();
    std::lock_guard<std::mutex> lock(service.control_mutex);
    if (!state_snapshot().connected || service.driver.setRGBMode(custom ? 1 : 0) != M5_TAB5_KB_OK) return false;
    std::lock_guard<std::mutex> state_lock(service.state_mutex);
    service.state.rgbMode = custom ? 1 : 0;
    return true;
}

bool HalEsp32::setKeyboardBrightness(uint8_t brightness)
{
    if (brightness > 100) return false;
    auto& service = keyboard();
    std::lock_guard<std::mutex> lock(service.control_mutex);
    if (!state_snapshot().connected || service.driver.setBrightness(brightness) != M5_TAB5_KB_OK) return false;
    std::lock_guard<std::mutex> state_lock(service.state_mutex);
    service.state.brightness = brightness;
    return true;
}
