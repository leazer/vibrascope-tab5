/**
 * @file probe_cdc_esp_idf.hpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-10-11
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "usb/usb_host.h"
#include "vhm/probe/byte_transport.hpp"

namespace vhm::probe {

/* Transport error counters; on CDC the only loss path is a full rx ring. */
struct CdcTransportStats {
    std::uint32_t ring_full{}; /* rx bytes dropped because the ring was full */
    std::uint32_t rx_high_water{};
};

/* IByteTransport over the ESP32-P4 USB-A port (OTG HS) speaking raw bulk
 * transfers against the probe's CDC data interface. A monitor task owns the
 * usb_host client, opens/claims the device and re-arms on disconnect; rx
 * data is collected by a small bank of QUEUED IN transfers that are
 * resubmitted from their completion callback, so a frame tail's short
 * packet never costs a task round-trip (the cdc_acm class driver's
 * per-frame complete/re-arm cycle capped the link at ~340 fps). */
class ProbeCdcTransport final : public IByteTransport {
public:
    bool begin();   /* idempotent: registers client once, starts monitor task */
    void end();     /* signals shutdown; device teardown happens with the board */
    bool set_baud(std::uint32_t baud) override; /* baud is meaningless on CDC */
    std::size_t write(const std::uint8_t* data, std::size_t size) override;
    std::size_t read(std::uint8_t* data, std::size_t capacity,
                     std::uint32_t timeout_ms) override;
    std::uint64_t now_ms() const override;
    CdcTransportStats stats() const;
    bool connected() const { return connected_.load(std::memory_order_acquire); }

private:
    static void client_event_cb(const usb_host_client_event_msg_t* event, void* arg);
    static void in_xfer_cb(usb_transfer_t* transfer);
    static void out_xfer_cb(usb_transfer_t* transfer);
    void monitor_loop();
    void try_open(std::uint8_t address);
    void teardown();
    void push_rx(const std::uint8_t* data, std::size_t len);

    static constexpr std::size_t kRingSize = 16384; /* ~100 ms at 155 KB/s */
    static constexpr int kInTransfers = 4;
    static constexpr int kInUrbBytes = 2048;  /* multiple of the 64 B MPS */
    static constexpr int kOutUrbBytes = 1024; /* one max protocol frame */

    usb_host_client_handle_t client_{nullptr};
    usb_device_handle_t dev_{nullptr};
    std::uint8_t itf_num_{0}, ep_in_{0}, ep_out_{0};
    std::uint8_t pending_addr_{0};
    bool gone_{false}, client_ready_{false};
    int in_outstanding_{0};
    usb_transfer_t* in_xfer_[kInTransfers]{};
    usb_transfer_t* out_xfer_{nullptr};
    SemaphoreHandle_t out_done_{nullptr};
    volatile usb_transfer_status_t out_status_{};
    void* monitor_task_{nullptr};
    mutable std::mutex mutex_;
    std::condition_variable rx_ready_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> stop_{false};
    bool initialized_{false};
    std::uint8_t ring_[kRingSize];
    std::size_t head_{0}, tail_{0}, buffered_{0};
    CdcTransportStats stats_{};
};

ProbeCdcTransport& probe_cdc();

}  // namespace vhm::probe
