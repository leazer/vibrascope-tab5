/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal/hal_esp32.h"
#include <mooncake_log.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <bsp/m5stack_tab5.h>
#include <esp_sleep.h>
#include <esp_check.h>
#include <esp_pm.h>

static const std::string _tag = "power";

hal::PowerStatus HalEsp32::getPowerStatus()
{
    std::lock_guard<std::mutex> lock(_status_mutex);
    return _power_status.state;
}

void HalEsp32::status_monitor_task(void* context)
{
    auto* self = static_cast<HalEsp32*>(context);
    unsigned ticks = 0;
    hal::AutoChargePolicy charge;
    for (;;) {
        if (ticks % 5 == 0) self->refresh_sd_card();
        float voltage = 0, current = 0;
        const bool valid = self->ina226.readSupplySample(voltage, current);
        const bool enabled = self->_charge_enable.load();
        hal::PowerStatus status;
        {
            std::lock_guard<std::mutex> lock(self->_status_mutex);
            self->_power_status.update(valid, voltage, current, enabled);
            status = self->_power_status.state;
        }
        // IP2326 VIN gates charging in hardware, including power-only USB adapters.
        const bool enable = charge.enable(status, enabled, current);
        if (enable != enabled) self->setChargeEnable(enable);
        if (++ticks % 10 == 0)
            mclog::tagInfo(_tag, "status valid={} battery={} charging={} mV={} mA={}", status.valid,
                static_cast<unsigned>(status.battery), status.charging, static_cast<int>(voltage * 1000),
                static_cast<int>(current * 1000));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void HalEsp32::status_monitor_init()
{
    if (xTaskCreate(status_monitor_task, "power-status", 3072, this, 2, nullptr) != pdPASS)
        mclog::tagError(_tag, "status monitor task allocation failed");
}

void HalEsp32::updatePowerMonitorData()
{
    // mclog::tagInfo(_tag, "update power monitor");
    powerMonitorData.busVoltage   = ina226.readBusVoltage();
    powerMonitorData.shuntVoltage = ina226.readShuntVoltage();
    powerMonitorData.busPower     = ina226.readBusPower();
    powerMonitorData.shuntCurrent = ina226.readShuntCurrent();
}

void HalEsp32::setChargeQcEnable(bool enable)
{
    _charge_qc_enable = enable;
    mclog::tagInfo(_tag, "set charge qc enable: {}", _charge_qc_enable);
    bsp_set_charge_qc_en(_charge_qc_enable);
}

bool HalEsp32::getChargeQcEnable()
{
    return _charge_qc_enable;
}

void HalEsp32::setChargeEnable(bool enable)
{
    _charge_enable = enable;
    mclog::tagInfo(_tag, "set charge enable: {}", _charge_enable.load());
    bsp_set_charge_en(_charge_enable.load());
}

bool HalEsp32::getChargeEnable()
{
    return _charge_enable;
}

void HalEsp32::setUsb5vEnable(bool enable)
{
    _usba_5v_enable = enable;
    mclog::tagInfo(_tag, "set usb 5v enable: {}", _usba_5v_enable);
    bsp_set_usb_5v_en(_usba_5v_enable);
}

bool HalEsp32::getUsb5vEnable()
{
    return _usba_5v_enable;
}

void HalEsp32::setExt5vEnable(bool enable)
{
    _ext_5v_enable = enable;
    mclog::tagInfo(_tag, "set ext 5v enable: {}", _ext_5v_enable);
    bsp_set_ext_5v_en(_ext_5v_enable);
}

bool HalEsp32::getExt5vEnable()
{
    return _ext_5v_enable;
}

void HalEsp32::powerOff()
{
    mclog::tagInfo(_tag, "power off");

    const auto brightness = getDisplayBrightness();
    setDisplayBrightness(0);
    playShutdownSfx();
    const auto started = millis();
    while (getMusicPlayTestState() != hal::HalBase::MUSIC_PLAY_IDLE && millis() - started < 3500) {
        delay(20);
    }
    auto* codec = bsp_get_codec_handle();
    codec->set_mute(true);
    delay(20);
    const auto amp_disabled = bsp_set_speaker_amp_enabled(false);
    if (amp_disabled == ESP_OK) delay(30);

    bsp_generate_poweroff_signal();
    delay(250);
    // A surviving CPU means the board did not latch the power-off pulse.
    if (amp_disabled == ESP_OK) bsp_set_speaker_amp_enabled(true);
    codec->set_mute(getSpeakerVolume() == 0);
    setDisplayBrightness(brightness);
}

extern esp_lcd_touch_handle_t _lcd_touch_handle;

void HalEsp32::sleepAndTouchWakeup()
{
    mclog::tagInfo(_tag, "sleep and touch wakeup");

    lvglLock();
    auto brightness = getDisplayBrightness();
    setDisplayBrightness(0);

    uint16_t touch_x[1];
    uint16_t touch_y[1];
    uint16_t touch_strength[1];
    uint8_t touch_cnt = 0;

    while (1) {
        esp_lcd_touch_read_data(_lcd_touch_handle);
        bool touchpad_pressed =
            esp_lcd_touch_get_coordinates(_lcd_touch_handle, touch_x, touch_y, touch_strength, &touch_cnt, 1);
        // mclog::tagInfo(_tag, "touchpad pressed: {}", touchpad_pressed);
        if (!touchpad_pressed) {
            break;
        }
        delay(100);
    }

    while (1) {
        esp_lcd_touch_read_data(_lcd_touch_handle);
        bool touchpad_pressed =
            esp_lcd_touch_get_coordinates(_lcd_touch_handle, touch_x, touch_y, touch_strength, &touch_cnt, 1);
        // mclog::tagInfo(_tag, "touchpad pressed: {}", touchpad_pressed);
        if (touchpad_pressed) {
            break;
        }
        delay(100);
    }

    // esp_restart();

    lvglUnlock();
    delay(500);
    setDisplayBrightness(brightness);
}

void HalEsp32::sleepAndRtcWakeup()
{
    mclog::tagInfo(_tag, "start sleep and rtc wakeup");

    clearRtcIrq();
    clearImuIrq();

    delay(200);

    mclog::tagInfo(_tag, "set rtc alarm");
    struct tm time;
    rx8130.getTime(&time);
    time.tm_hour = 0;
    time.tm_min  = 1;
    time.tm_sec  = 49;
    rx8130.setTime(&time);
    time.tm_hour = 0;
    time.tm_min  = 2;
    time.tm_sec  = 0;
    rx8130.setAlarmIrq(&time);

    // delay(800);
    powerOff();
    while (1) {
        delay(100);
    }
}
