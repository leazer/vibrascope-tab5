/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <hal/hal.h>
#include <ina226.hpp>
#include <lvgl.h>
#include "utils/rx8130/rx8130.h"
#include <atomic>

class HalEsp32 : public hal::HalBase {
public:
    std::string type() override
    {
        return "Tab5";
    }

    void init() override;
    hal::DevicePreferences getDevicePreferences() override { return _device_preferences; }
    bool saveDevicePreferences(const hal::DevicePreferences& preferences) override;

    void delay(uint32_t ms) override;
    uint32_t millis() override;
    int getCpuTemp() override;

    INA226 ina226;
    RX8130_Class rx8130;
    lv_disp_t* lvDisp      = nullptr;
    lv_indev_t* lvKeyboard = nullptr;

    void setDisplayBrightness(uint8_t brightness) override;
    uint8_t getDisplayBrightness() override;
    std::string getDisplayPanelIc() override;

    void lvglLock() override;
    void lvglUnlock() override;

    void updatePowerMonitorData() override;
    hal::PowerStatus getPowerStatus() override;
    hal::ConnectionState getComputerUsbState() override;
    hal::WirelessStatus getWirelessStatus() override;
    void updateImuData() override;
    void clearImuIrq() override;

    void clearRtcIrq() override;
    void setRtcTime(tm time) override;

    void setChargeQcEnable(bool enable) override;
    bool getChargeQcEnable() override;
    void setChargeEnable(bool enable) override;
    bool getChargeEnable() override;
    void setUsb5vEnable(bool enable) override;
    bool getUsb5vEnable() override;
    void setExt5vEnable(bool enable) override;
    bool getExt5vEnable() override;
    void powerOff() override;
    void sleepAndTouchWakeup() override;
    void sleepAndShakeWakeup() override;
    void sleepAndRtcWakeup() override;

    void startCameraCapture(lv_obj_t* imgCanvas) override;
    void stopCameraCapture() override;
    bool isCameraCapturing() override;

    void setSpeakerVolume(uint8_t volume) override;
    uint8_t getSpeakerVolume() override;
    void audioRecord(std::vector<int16_t>& data, uint16_t durationMs, float gain = 80.0f) override;
    void audioPlay(std::vector<int16_t>& data, bool async = true) override;
    void startDualMicRecordTest() override;
    MicTestState_t getDualMicRecordTestState() override;
    void startHeadphoneMicRecordTest() override;
    MicTestState_t getHeadphoneMicRecordTestState() override;
    void startPlayMusicTest() override;
    MusicPlayState_t getMusicPlayTestState() override;
    void stopPlayMusicTest() override;
    void playStartupSfx() override;
    void playShutdownSfx();
    void playKeyClick() override;
    void playDeleteVoice() override;

    void setExtAntennaEnable(bool enable) override;
    bool getExtAntennaEnable() override;
    void startWifiAp() override;

    bool isSdCardMounted() override;
    hal::ConnectionState getSdCardState() override;
    bool getSdCardUsage(std::uint64_t& used, std::uint64_t& total) override;
    bool beginSdRecording() override;
    void endSdRecording(bool success) override;
    std::string exportSdCard(const std::atomic<bool>& alive, const std::atomic<bool>& requested) override;
    std::vector<FileEntry_t> scanSdCard(const std::string& dirPath) override;

    bool usbCDetect() override;
    bool usbADetect() override;
    bool headPhoneDetect() override;
    hal::KeyboardState getKeyboardState() override;
    bool popKeyboardPress(hal::KeyboardPress& press) override;
    void requestKeyboardStatusColor(std::uint32_t rgb) override;
    bool setKeyboardRgb(uint8_t index, uint8_t r, uint8_t g, uint8_t b) override;
    bool setKeyboardRgbMode(bool custom) override;
    bool setKeyboardBrightness(uint8_t brightness) override;
    std::vector<uint8_t> i2cScan(bool isInternal) override;
    void initPortAI2c() override;
    void deinitPortAI2c() override;
    void gpioInitOutput(uint8_t pin) override;
    void gpioSetLevel(uint8_t pin, bool level) override;
    void gpioReset(uint8_t pin) override;

private:
    void set_gpio_output_capability();
    void hid_init();
    void probe_cdc_init();
    void keyboard_init();
    void status_monitor_init();
    void refresh_sd_card();
    std::mutex _sd_mutex;
    std::mutex _sd_usage_mutex;
    std::uint64_t _sd_used_bytes{}, _sd_total_bytes{};
    bool _sd_recording{};
    std::atomic<hal::ConnectionState> _usb_disk_state{hal::ConnectionState::Off};
    std::atomic<hal::ConnectionState> _sd_state{hal::ConnectionState::Connecting};
    static void status_monitor_task(void* context);
    std::mutex _status_mutex;
    hal::PowerStatusFilter _power_status;
    bool wifi_init();
    void imu_init();
    void update_system_time();

    hal::DevicePreferences _device_preferences;
    bool _device_nvs_ready{};
    uint8_t _current_lcd_brightness = 100;
    bool _charge_qc_enable          = false;
    std::atomic<bool> _charge_enable{true};
    bool _ext_5v_enable             = true;
    bool _usba_5v_enable            = true;
    bool _ext_antenna_enable        = false;
    std::atomic<bool> _sd_card_mounted{false};
    bool _keyboard_i2c_reserved     = false;
};
