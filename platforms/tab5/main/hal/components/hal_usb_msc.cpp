/**
 * @file hal_usb_msc.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-08
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "hal/hal_esp32.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <bsp/m5stack_tab5.h>
#include <driver/usb_serial_jtag.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_private/usb_phy.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/usb_serial_jtag_ll.h>
#include <hal/usb_wrap_ll.h>
#include <sdmmc_cmd.h>
#include <tusb.h>

namespace {
constexpr char kTag[] = "sd-usb-msc";
constexpr std::size_t kSectorBufferSize = 4096;
constexpr unsigned kPort = 0; // ESP32-P4 OTG1.1, independent of the USB-A HS host.
constexpr unsigned kDescriptorSize = TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN;
const tusb_desc_device_t kDeviceDescriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303a,
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};
const std::uint8_t kConfiguration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, kDescriptorSize, 0, 500),
    TUD_MSC_DESCRIPTOR(0, 0, 0x01, 0x81, 64),
};

// The recorder task services TinyUSB and is the only reader/writer of these
// fields. The IRQ queues events; it never calls the storage callbacks.
sdmmc_card_t* disk_card{};
std::uint8_t* sector_buffer{};
std::size_t sector_buffer_size{};
bool disk_mounted{}, disk_ejected{};
char disk_serial[13] = "000000000000";

void delay_ms(unsigned ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

std::string run_disk(sdmmc_card_t& card, std::atomic<hal::ConnectionState>& state,
                     const std::atomic<bool>& alive, const std::atomic<bool>& requested)
{
    const auto sector_size = card.csd.sector_size;
    if (!sector_size || sector_size > kSectorBufferSize || card.csd.capacity > UINT32_MAX)
        return "SD 导出失败";
    sector_buffer_size = kSectorBufferSize;
    sector_buffer = static_cast<std::uint8_t*>(
        heap_caps_aligned_alloc(64, sector_buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    if (!sector_buffer && sector_size < sector_buffer_size) {
        sector_buffer_size = sector_size;
        sector_buffer = static_cast<std::uint8_t*>(
            heap_caps_aligned_alloc(64, sector_buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    }
    if (!sector_buffer) {
        ESP_LOGE(kTag, "MSC DMA buffer allocation failed: need=%u largest=%u",
                 static_cast<unsigned>(sector_buffer_size),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)));
        return "USB 导出内存不足";
    }
    disk_card = &card;
    disk_mounted = disk_ejected = false;
    std::uint8_t mac[6]{};
    if (esp_efuse_mac_get_default(mac) == ESP_OK)
        std::snprintf(disk_serial, sizeof(disk_serial), "%02X%02X%02X%02X%02X%02X",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    usb_phy_config_t phy_config{};
    phy_config.controller = USB_PHY_CTRL_OTG;
    phy_config.target = USB_PHY_TARGET_INT;
    phy_config.otg_mode = USB_OTG_MODE_DEVICE;
    phy_config.otg_speed = USB_PHY_SPEED_FULL;
    usb_phy_handle_t phy = nullptr;
    auto status = usb_new_phy(&phy_config, &phy);
    bool enumerated = false;
    bool ejected = false;
    if (status == ESP_OK) {
        // Keep the FS pads detached while initializing the controller. GPIO27
        // is used for the board's audio clock when the PHY is in default routing.
        USB_WRAP.otg_conf.usb_pad_enable = 0;
        const tusb_rhport_init_t init{.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
        if (tusb_init(kPort, &init)) {
            tud_disconnect();
            usb_serial_jtag_ll_phy_enable_pad(false);
            usb_wrap_ll_phy_select(&USB_WRAP, 0);
            delay_ms(240);
            USB_WRAP.otg_conf.usb_pad_enable = 1;
            tud_connect();
            // Allow Windows time to re-enumerate from COM to removable storage.
            const auto started = xTaskGetTickCount();
            while (alive && requested && !disk_ejected &&
                   (disk_mounted || xTaskGetTickCount() - started < pdMS_TO_TICKS(12000))) {
                tud_task_ext(20, false);
                if (disk_mounted && !disk_ejected && !enumerated) {
                    enumerated = true;
                    state = hal::ConnectionState::Connected;
                }
            }
            ejected = disk_ejected;
            if (ejected) {
                // Complete the host's eject command before disconnecting the port.
                const auto eject_started = xTaskGetTickCount();
                while (xTaskGetTickCount() - eject_started < pdMS_TO_TICKS(180))
                    tud_task_ext(20, false);
            }
            tud_disconnect();
            tusb_deinit(kPort);
            USB_WRAP.otg_conf.usb_pad_enable = 0;
            usb_wrap_ll_phy_select(&USB_WRAP, 1);
            delay_ms(240);
            usb_serial_jtag_ll_phy_enable_pad(true);
        } else {
            status = ESP_FAIL;
        }
        usb_del_phy(phy);
    }
    disk_card = nullptr;
    heap_caps_free(sector_buffer);
    sector_buffer = nullptr;
    sector_buffer_size = 0;
    ESP_LOGI(kTag, "MSC ended: enumerated=%d ejected=%d init=%s", enumerated, ejected,
             esp_err_to_name(status));
    if (status != ESP_OK) return "USB 导出启动失败";
    if (!enumerated && requested && alive) return "USB 磁盘连接失败";
    return "磁盘模式断开";
}
}  // namespace

std::string HalEsp32::exportSdCard(const std::atomic<bool>& alive, const std::atomic<bool>& requested)
{
    if (!usb_serial_jtag_is_connected()) return "请连接电脑 USB";
    if (!beginSdRecording()) return "SD 未就绪";

    static char mount_point[] = "/sd";
    _usb_disk_state = hal::ConnectionState::Connecting;
    _sd_card_mounted = false;
    _sd_state = hal::ConnectionState::Connecting;
    auto result = bsp_sdcard_deinit(mount_point);
    sdmmc_card_t raw{};
    if (result == ESP_OK) result = bsp_sdcard_raw_init(&raw);
    std::string message = "SD 导出失败";
    if (result == ESP_OK) {
        if (alive && requested) message = run_disk(raw, _usb_disk_state, alive, requested);
        else message = "磁盘模式断开";
        sdmmc_host_deinit_slot(raw.host.slot);
    }
    _usb_disk_state = hal::ConnectionState::Off;
    {
        std::lock_guard<std::mutex> lock(_sd_mutex);
        result = bsp_sdcard_init(mount_point, 8);
        _sd_card_mounted = result == ESP_OK;
        _sd_state = result == ESP_OK ? hal::ConnectionState::Connected : hal::ConnectionState::Fault;
        _sd_recording = false;
    }
    if (result != ESP_OK) return "SD 未就绪";
    return message;
}

extern "C" {
const std::uint8_t* tud_descriptor_device_cb()
{
    return reinterpret_cast<const std::uint8_t*>(&kDeviceDescriptor);
}

const std::uint8_t* tud_descriptor_configuration_cb(std::uint8_t index)
{
    return index == 0 ? kConfiguration : nullptr;
}

const std::uint16_t* tud_descriptor_string_cb(std::uint8_t index, std::uint16_t)
{
    static std::uint16_t descriptor[32];
    const char* strings[] = {"", "VibraNode", "VibraScope SD", disk_serial};
    if (index == 0) {
        descriptor[0] = (TUSB_DESC_STRING << 8) | 4;
        descriptor[1] = 0x0409;
    } else {
        if (index >= sizeof(strings) / sizeof(strings[0])) return nullptr;
        const auto len = std::strlen(strings[index]);
        for (std::size_t i = 0; i < len; ++i) descriptor[1 + i] = strings[index][i];
        descriptor[0] = (TUSB_DESC_STRING << 8) | (2 + 2 * len);
    }
    return descriptor;
}

void tud_mount_cb() { disk_mounted = true; }
void tud_umount_cb()
{
    if (disk_mounted) disk_ejected = true;
}

void tud_msc_inquiry_cb(std::uint8_t, std::uint8_t vendor[8], std::uint8_t product[16], std::uint8_t version[4])
{
    std::memcpy(vendor, "VibraNod", 8);
    std::memcpy(product, "VibraScope SD   ", 16);
    std::memcpy(version, "1.0 ", 4);
}

bool tud_msc_is_writable_cb(std::uint8_t) { return disk_card && !disk_ejected; }

bool tud_msc_test_unit_ready_cb(std::uint8_t lun)
{
    if (disk_card && !disk_ejected) return true;
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0);
    return false;
}

void tud_msc_capacity_cb(std::uint8_t, std::uint32_t* blocks, std::uint16_t* size)
{
    *blocks = disk_card ? static_cast<std::uint32_t>(disk_card->csd.capacity) : 0;
    *size = disk_card ? disk_card->csd.sector_size : 512;
}

bool tud_msc_start_stop_cb(std::uint8_t, std::uint8_t, bool start, bool eject)
{
    if (eject && !start) disk_ejected = true;
    return true;
}

static std::int32_t transfer_sectors(std::uint8_t lun, std::uint32_t lba, std::uint32_t offset,
                                     void* data, std::uint32_t size, bool write)
{
    if (!tud_msc_test_unit_ready_cb(lun)) return -1;
    const std::uint32_t sector = disk_card->csd.sector_size;
    std::uint64_t position = static_cast<std::uint64_t>(lba) * sector + offset;
    const auto capacity = static_cast<std::uint64_t>(disk_card->csd.capacity) * sector;
    if (position > capacity || size > capacity - position) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0);
        return -1;
    }
    auto* buffer = static_cast<std::uint8_t*>(data);
    std::uint32_t remaining = size;
    while (remaining) {
        const std::size_t skip = position % sector;
        const auto bytes = std::min<std::size_t>(remaining, sector_buffer_size / sector * sector - skip);
        const auto blocks = (skip + bytes + sector - 1) / sector;
        const auto first_block = position / sector;
        // Preserve the rest of the first/last block when USB delivers a partial write.
        if ((!write || skip || bytes % sector) &&
            sdmmc_read_sectors(disk_card, sector_buffer, first_block, blocks) != ESP_OK) {
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0);
            return -1;
        }
        if (write) {
            std::memcpy(sector_buffer + skip, buffer, bytes);
            if (sdmmc_write_sectors(disk_card, sector_buffer, first_block, blocks) != ESP_OK) {
                tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0c, 0);
                return -1;
            }
        } else {
            std::memcpy(buffer, sector_buffer + skip, bytes);
        }
        buffer += bytes;
        position += bytes;
        remaining -= bytes;
    }
    return static_cast<std::int32_t>(size);
}

std::int32_t tud_msc_read10_cb(std::uint8_t lun, std::uint32_t lba, std::uint32_t offset,
                             void* data, std::uint32_t size)
{
    return transfer_sectors(lun, lba, offset, data, size, false);
}

std::int32_t tud_msc_write10_cb(std::uint8_t lun, std::uint32_t lba, std::uint32_t offset,
                              std::uint8_t* data, std::uint32_t size)
{
    return transfer_sectors(lun, lba, offset, data, size, true);
}

std::int32_t tud_msc_scsi_cb(std::uint8_t lun, const std::uint8_t command[16], void*, std::uint16_t)
{
    // SYNCHRONIZE CACHE: WRITE10 completes synchronously on the SD card.
    if (command[0] == 0x35) return tud_msc_test_unit_ready_cb(lun) ? 0 : -1;
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0);
    return -1;
}
}  // extern "C"
