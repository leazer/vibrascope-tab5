#pragma once

#include <cstdint>

#include "vhm/domain/sample_batch.hpp"

namespace vhm {

struct MeasurementConfig {
    std::uint32_t config_id{0};
    std::uint32_t adxl_rate_hz{2000};
    std::uint16_t adxl_range_g{10};
    std::uint32_t icm_rate_hz{200};
    std::uint16_t icm_accel_range_g{16};
    std::uint16_t icm_gyro_range_dps{2000};
    bool attitude_enabled{true};
};

enum class SourceResult : std::uint8_t {
    Ok,
    Timeout,
    EndOfStream,
    Busy,
    InvalidConfig,
    TransportError,
};

class ISampleSource {
public:
    virtual ~ISampleSource() = default;

    virtual SourceResult start(const MeasurementConfig& config) = 0;
    virtual SourceResult read(SampleBatch& batch, std::uint32_t timeout_ms) = 0;
    virtual SourceResult stop() = 0;
};

}  // namespace vhm
