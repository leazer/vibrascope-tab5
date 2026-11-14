/**
 * @file app_link_validation.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-09-10
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <mooncake.h>

enum class ValidationCommand : std::uint8_t { None, Connect, StartSlow, StartDefault, StartHigh, Stop };
enum class ValidationStage : std::uint8_t { Disconnected, Standby, Measuring, Stopping, Fault };

struct ValidationSnapshot {
    ValidationStage stage{ValidationStage::Disconnected};
    std::uint64_t adxl_samples{0};
    std::uint64_t icm_samples{0};
    std::uint32_t crc_errors{0};
    std::uint32_t timeouts{0};
    std::uint32_t retries{0};
    std::uint32_t bad_sessions{0};
    std::uint32_t sequence_gaps{0};
    std::uint32_t link_errors{0};
    std::uint32_t rx_high_water{0};
    std::uint32_t probe_queue_drops{0};
    std::uint32_t probe_fifo_overflows{0};
    std::uint32_t probe_format_errors{0};
};

class LinkValidationRuntime {
public:
    void start();
    void request(ValidationCommand command) { command_.store(command); }
    ValidationSnapshot snapshot() const;
    void update_desktop();
    void add_samples(bool adxl, std::uint16_t count);

private:
    friend void link_validation_worker(void*);
    std::atomic<ValidationCommand> command_{ValidationCommand::None};
    std::atomic<ValidationStage> stage_{ValidationStage::Disconnected};
    std::atomic<std::uint64_t> adxl_samples_{0}, icm_samples_{0};
    std::atomic<std::uint32_t> crc_errors_{0}, timeouts_{0}, retries_{0};
    std::atomic<std::uint32_t> bad_sessions_{0}, sequence_gaps_{0};
    std::atomic<std::uint32_t> link_errors_{0}, rx_high_water_{0};
    std::atomic<std::uint32_t> probe_queue_drops_{0}, probe_fifo_overflows_{0};
    std::atomic<std::uint32_t> probe_format_errors_{0};
};

LinkValidationRuntime& link_validation_runtime();

class AppLinkValidation : public mooncake::AppAbility {
public:
    AppLinkValidation();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void* screen_{nullptr};
    void* status_label_{nullptr};
    void* counters_label_{nullptr};
};
