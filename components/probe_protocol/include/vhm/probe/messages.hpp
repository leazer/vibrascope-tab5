#pragma once

#include <array>
#include <cstdint>

#include "vhm/domain/sample_batch.hpp"
#include "vhm/probe/frame_codec.hpp"

namespace vhm::probe {

enum class ResultCode : std::uint8_t {
    Ok = 0, Busy = 1, BadSession = 2, Malformed = 3, InvalidParam = 4,
    UnsupportedConfig = 5, NotReady = 6, InProgress = 7,
    NotImplemented = 8, InternalError = 9, CalibrationRejected = 10,
};

enum class ProbeState : std::uint8_t {
    Uninitialized = 0, Standby = 1, Measuring = 2, Calibrating = 3,
    Warning = 4, Fault = 5,
};

struct StreamConfig {
    std::uint8_t stream_id{0};
    std::uint8_t odr_code{0};
    std::uint8_t range_code{0};
    std::uint8_t filter_code{0};
};

struct Result { std::uint32_t request_id{0}; ResultCode code{ResultCode::Ok}; };
struct HelloResponse {
    Result result{}; std::uint16_t probe_type{0}; std::uint8_t hardware_version{0};
    std::uint8_t firmware_major{0}, firmware_minor{0}; std::uint16_t firmware_build{0};
    std::uint32_t uptime_ms{0}; std::uint16_t supported_baud_mask{0};
    std::uint16_t sensor_mask{0}; std::uint32_t config_id{0};
    ProbeState state{ProbeState::Uninitialized};
};
struct ConfigResponse { Result result{}; std::uint8_t stream_count{0}; std::array<StreamConfig, 2> streams{}; std::uint8_t rejected_stream{0}; };
struct StopBoundary { std::uint8_t stream_id{0}; std::uint64_t last_sequence{0}; };
struct StopResponse { Result result{}; std::array<StopBoundary, 2> streams{}; };
struct StreamStatus {
    std::uint8_t stream_id{0}; std::uint64_t total_samples{0}; std::uint32_t segment{0};
    std::uint32_t sequence_gaps{0}; std::uint16_t queued_batches{0}; std::uint32_t queued_bytes{0};
    std::uint32_t spi_errors{0}, fifo_overflows{0}, format_time_errors{0}, queue_drops{0};
};
struct ProbeStatus {
    std::uint32_t uptime_ms{0}; ProbeState state{ProbeState::Uninitialized};
    std::uint16_t fault_bits{0}; std::uint32_t config_id{0}; std::array<StreamStatus, 2> streams{};
    std::uint32_t crc_errors{0}, bad_types{0}, bad_sessions{0}, busy_rejects{0};
    ResultCode last_result{ResultCode::Ok};
};
struct FetchBacklog { std::uint16_t batches{0}; std::uint32_t bytes{0}; };
struct FetchDone {
    Result result{}; std::uint32_t bytes_sent{0}; std::uint16_t frames_sent{0};
    std::array<FetchBacklog, 2> backlog{}; ProbeState state{ProbeState::Uninitialized};
};
struct CalibrationStatus {
    Result result{}; std::uint8_t state{0}, target{0}, face{0}, completed_faces{0};
    std::uint8_t valid_candidates{0}, candidate_modified{0}; ResultCode window_result{ResultCode::Ok};
    std::uint32_t sample_count{0};
};

enum class DecodeError : std::uint8_t {
    None,
    WrongType,
    WrongLength,
    InvalidField,
    TooManySamples,
};

struct AttitudeSample {
    std::uint32_t config_id{0};
    std::uint32_t segment{0};
    std::uint64_t sequence{0};
    std::uint64_t time_us{0};
    std::uint32_t period_q16{0};
    vhm::Quality quality{vhm::Quality::None};
    std::array<std::int32_t, 4> quat_q30{};
};
struct TemperatureSample {
    std::uint32_t config_id{};
    std::uint64_t time_us{};
    std::uint16_t raw{};
    bool valid{}, live{};
};
DecodeError decode_temperature(const Frame& frame, TemperatureSample& output);

struct CalibrationRecord {
    std::uint32_t request_id{0};
    ResultCode result{ResultCode::Ok};
    std::uint8_t version{0};
    std::uint8_t valid_flags{0};
    std::uint32_t generation{0};
    std::uint32_t created_ms{0};
    std::array<float, 3> adxl_accel_bias_g{};
    std::array<float, 3> adxl_accel_scale{};
    std::array<float, 3> icm_accel_bias_g{};
    std::array<float, 3> icm_accel_scale{};
    std::array<float, 3> icm_gyro_bias_dps{};
    float temperature_c{0.0F};
};

DecodeError decode_stream(const Frame& frame, vhm::SampleBatch& output);
DecodeError decode_attitude(const Frame& frame, AttitudeSample& output);
DecodeError decode_calibration_record(const Frame& frame, CalibrationRecord& output);

FrameBytes encode_hello_request(std::uint32_t frame_sequence, std::uint32_t request_id, std::uint32_t new_session);
FrameBytes encode_set_link_request(std::uint32_t session, std::uint32_t frame_sequence, std::uint32_t request_id, std::uint32_t baud);
FrameBytes encode_set_config_request(std::uint32_t session, std::uint32_t frame_sequence, std::uint32_t request_id, std::uint32_t config_id, const StreamConfig* streams, std::uint8_t stream_count);
FrameBytes encode_start_request(std::uint32_t session, std::uint32_t frame_sequence, std::uint32_t request_id);
FrameBytes encode_fetch_request(std::uint32_t session, std::uint32_t frame_sequence, std::uint32_t request_id, std::uint32_t byte_budget);
FrameBytes encode_stop_request(std::uint32_t session, std::uint32_t frame_sequence, std::uint32_t request_id);
FrameBytes encode_get_status_request(std::uint32_t session, std::uint32_t frame_sequence, std::uint32_t request_id);
FrameBytes encode_calibrate_request(std::uint32_t session, std::uint32_t frame_sequence, std::uint32_t request_id, std::uint8_t action, std::uint8_t target, std::uint8_t face);
FrameBytes encode_save_calibration_request(std::uint32_t session, std::uint32_t frame_sequence, std::uint32_t request_id, std::uint8_t action);
DecodeError decode_result(const Frame& frame, MessageType expected_type, Result& output);
DecodeError decode_hello(const Frame& frame, HelloResponse& output);
DecodeError decode_set_config(const Frame& frame, ConfigResponse& output);
DecodeError decode_stop(const Frame& frame, StopResponse& output);
DecodeError decode_status(const Frame& frame, ProbeStatus& output);
DecodeError decode_fetch_done(const Frame& frame, FetchDone& output);
DecodeError decode_calibration_status(const Frame& frame, CalibrationStatus& output);

}  // namespace vhm::probe
