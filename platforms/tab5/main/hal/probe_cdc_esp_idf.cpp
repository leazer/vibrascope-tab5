/**
 * @file probe_cdc_esp_idf.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-10-11
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "hal/probe_cdc_esp_idf.hpp"
#include <chrono>
#include <cstring>
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

namespace vhm::probe {
namespace {
constexpr const char* kTag = "probe_cdc";
constexpr uint8_t kDescInterface = 0x04;
constexpr uint8_t kDescEndpoint = 0x05;
constexpr uint8_t kXferBulk = 0x02;
}  // namespace

bool ProbeCdcTransport::begin()
{
    if (initialized_) return true;
    out_done_ = xSemaphoreCreateBinary();
    if (!out_done_) return false;
    for (int i = 0; i < kInTransfers; ++i) {
        if (usb_host_transfer_alloc(kInUrbBytes, 0, &in_xfer_[i]) != ESP_OK) {
            ESP_LOGE(kTag, "in transfer alloc failed");
            return false;
        }
        in_xfer_[i]->callback = &ProbeCdcTransport::in_xfer_cb;
        in_xfer_[i]->context = this;
    }
    if (usb_host_transfer_alloc(kOutUrbBytes, 0, &out_xfer_) != ESP_OK) {
        ESP_LOGE(kTag, "out transfer alloc failed");
        return false;
    }
    out_xfer_->callback = &ProbeCdcTransport::out_xfer_cb;
    out_xfer_->context = this;

    usb_host_client_config_t client = {};
    client.is_synchronous = false;
    client.max_num_event_msg = 10;
    client.async.client_event_callback = &ProbeCdcTransport::client_event_cb;
    client.async.callback_arg = this;
    if (usb_host_client_register(&client, &client_) != ESP_OK) {
        ESP_LOGE(kTag, "client register failed");
        return false;
    }
    TaskHandle_t task = nullptr;
    if (xTaskCreate(
            [](void* p) { static_cast<ProbeCdcTransport*>(p)->monitor_loop(); },
            "probe_cdc", 4096, this, 9, &task) != pdTRUE) {
        usb_host_client_deregister(client_);
        return false;
    }
    monitor_task_ = task;
    initialized_ = true;
    ESP_LOGI(kTag, "raw cdc transport started, waiting for probe");
    return true;
}

void ProbeCdcTransport::end()
{
    stop_.store(true, std::memory_order_relaxed);
    connected_.store(false, std::memory_order_relaxed);
}

bool ProbeCdcTransport::set_baud(std::uint32_t /*baud*/)
{
    return true; /* SET_LINK rides the frozen protocol; line speed is USB */
}

std::size_t ProbeCdcTransport::write(const std::uint8_t* data, std::size_t size)
{
    if (!connected_.load(std::memory_order_acquire)) return 0;
    usb_device_handle_t dev;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dev = dev_;
    }
    if (!dev || !size) return 0;
    if (size > (std::size_t)kOutUrbBytes) size = kOutUrbBytes;
    memcpy(out_xfer_->data_buffer, data, size);
    out_xfer_->num_bytes = size;
    out_xfer_->device_handle = dev;
    xSemaphoreTake(out_done_, 0); /* absorb a stale completion */
    out_status_ = USB_TRANSFER_STATUS_ERROR;
    if (usb_host_transfer_submit(out_xfer_) != ESP_OK) return 0;
    /* Frames are <= ~1.1 KB and FS bulk OUT moves ~1 MB/s; 100 ms only
     * catches a disconnected cable. Residual window: disconnect landing
     * between the submit and the completion - usbh completes in-flight
     * transfers before delivering DEV_GONE, so this is bounded. */
    if (xSemaphoreTake(out_done_, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    if (out_status_ != USB_TRANSFER_STATUS_COMPLETED) return 0;
    return size;
}

std::size_t ProbeCdcTransport::read(std::uint8_t* out, std::size_t capacity,
                                    std::uint32_t timeout_ms)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!buffered_ &&
        !rx_ready_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [this] { return buffered_ != 0; }))
        return 0;
    const std::size_t moved = capacity < buffered_ ? capacity : buffered_;
    for (std::size_t i = 0; i < moved; ++i) {
        out[i] = ring_[tail_];
        tail_ = (tail_ + 1) % kRingSize;
    }
    buffered_ -= moved;
    return moved;
}

std::uint64_t ProbeCdcTransport::now_ms() const
{
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

CdcTransportStats ProbeCdcTransport::stats() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void ProbeCdcTransport::push_rx(const std::uint8_t* data, std::size_t len)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const bool was_empty = buffered_ == 0;
    for (std::size_t i = 0; i < len; ++i) {
        if (buffered_ == kRingSize) {
            ++stats_.ring_full;
            break;
        }
        ring_[head_] = data[i];
        head_ = (head_ + 1) % kRingSize;
        ++buffered_;
    }
    if (buffered_ > stats_.rx_high_water)
        stats_.rx_high_water = static_cast<std::uint32_t>(buffered_);
    if (was_empty && buffered_) rx_ready_.notify_one();
}

void ProbeCdcTransport::client_event_cb(const usb_host_client_event_msg_t* event,
                                        void* arg)
{
    auto* self = static_cast<ProbeCdcTransport*>(arg);
    if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        if (!self->pending_addr_) self->pending_addr_ = event->new_dev.address;
    } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        self->gone_ = true;
    }
}

void ProbeCdcTransport::in_xfer_cb(usb_transfer_t* xfer)
{
    auto* self = static_cast<ProbeCdcTransport*>(xfer->context);
    --self->in_outstanding_;
    if (xfer->status == USB_TRANSFER_STATUS_COMPLETED && xfer->actual_num_bytes > 0)
        self->push_rx(xfer->data_buffer, (std::size_t)xfer->actual_num_bytes);
    /* Resubmit from inside the completion callback (same task that runs the
     * client): the pipe never sits un-armed while a task round-trips, which
     * is what capped the cdc_acm class driver at ~340 fps. */
    if (self->dev_ && xfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        xfer->num_bytes = kInUrbBytes;
        if (usb_host_transfer_submit(xfer) == ESP_OK) ++self->in_outstanding_;
    }
}

void ProbeCdcTransport::out_xfer_cb(usb_transfer_t* xfer)
{
    auto* self = static_cast<ProbeCdcTransport*>(xfer->context);
    self->out_status_ = xfer->status;
    xSemaphoreGive(self->out_done_);
}

void ProbeCdcTransport::try_open(std::uint8_t address)
{
    usb_device_handle_t dev = nullptr;
    if (usb_host_device_open(client_, address, &dev) != ESP_OK) return;
    const usb_config_desc_t *cfg = nullptr;
    if (usb_host_get_active_config_descriptor(dev, &cfg) != ESP_OK || !cfg) {
        usb_host_device_close(client_, dev);
        return;
    }
    /* Walk the descriptors for the interface carrying a bulk IN endpoint
     * (the CDC data interface on the probe: comm class first, data second). */
    const uint8_t *p = reinterpret_cast<const uint8_t *>(cfg);
    const uint8_t *end = p + cfg->wTotalLength;
    uint8_t cur_itf = 0, itf = 0, ep_in = 0, ep_out = 0;
    while (p + 2 <= end && p + p[0] <= end) {
        const uint8_t len = p[0], type = p[1];
        if (type == kDescInterface && len >= 4) {
            cur_itf = p[2];
        } else if (type == kDescEndpoint && len >= 6 && (p[3] & 0x03) == kXferBulk) {
            const uint8_t addr = p[2];
            if (addr & 0x80) {
                if (!ep_in) { ep_in = addr; itf = cur_itf; }
            } else if (!ep_out) {
                ep_out = addr;
            }
        }
        p += len;
    }
    if (!ep_in ||
        usb_host_interface_claim(client_, dev, itf, 0) != ESP_OK) {
        usb_host_device_close(client_, dev);
        ESP_LOGW(kTag, "probe device %u: no claimable bulk interface", address);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = tail_ = buffered_ = 0;
        dev_ = dev;
        itf_num_ = itf;
        ep_in_ = ep_in;
        ep_out_ = ep_out;
    }
    out_xfer_->bEndpointAddress = ep_out;
    connected_.store(true, std::memory_order_release);
    in_outstanding_ = 0;
    for (int i = 0; i < kInTransfers; ++i) {
        in_xfer_[i]->device_handle = dev;
        in_xfer_[i]->bEndpointAddress = ep_in_;
        in_xfer_[i]->num_bytes = kInUrbBytes;
        if (usb_host_transfer_submit(in_xfer_[i]) == ESP_OK) ++in_outstanding_;
    }
    ESP_LOGI(kTag, "probe cdc connected (itf %u, bulk %02x/%02x)", itf, ep_in, ep_out);
}

void ProbeCdcTransport::teardown()
{
    connected_.store(false, std::memory_order_release);
    usb_device_handle_t dev;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dev = dev_;
        dev_ = nullptr; /* in-flight completions stop resubmitting */
    }
    /* The queued IN transfers complete (with errors) once the host halts
     * the pipe; drain those completions before releasing the interface. */
    for (int i = 0; i < 20 && in_outstanding_ > 0; ++i)
        usb_host_client_handle_events(client_, pdMS_TO_TICKS(10));
    usb_host_interface_release(client_, dev, itf_num_);
    usb_host_device_close(client_, dev);
    ESP_LOGI(kTag, "probe cdc disconnected");
}

void ProbeCdcTransport::monitor_loop()
{
    while (!stop_.load(std::memory_order_relaxed)) {
        usb_host_client_handle_events(client_, pdMS_TO_TICKS(50));
        if (pending_addr_ && !dev_) {
            const uint8_t addr = pending_addr_;
            pending_addr_ = 0;
            try_open(addr);
        }
        if (gone_ && dev_) {
            gone_ = false;
            teardown();
        }
    }
    vTaskDelete(nullptr);
}

ProbeCdcTransport& probe_cdc()
{
    static ProbeCdcTransport instance;
    return instance;
}

}  // namespace vhm::probe
