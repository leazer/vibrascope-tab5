#include "vhm/probe/messages.hpp"

#include <cstring>

namespace vhm::probe {
namespace {

std::uint16_t read_u16_le(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(p[1] << 8);
}

std::uint32_t read_u32_le(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t read_u64_le(const std::uint8_t* p)
{
    return static_cast<std::uint64_t>(read_u32_le(p)) |
           (static_cast<std::uint64_t>(read_u32_le(p + 4)) << 32);
}

std::int16_t read_i16(const std::uint8_t* p)
{
    return static_cast<std::int16_t>(read_u16_le(p));
}

std::int32_t read_i32(const std::uint8_t* p)
{
    return static_cast<std::int32_t>(read_u32_le(p));
}

float read_float(const std::uint8_t* p)
{
    const std::uint32_t bits = read_u32_le(p);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void write_u16_le(std::uint8_t* p, std::uint16_t value)
{
    p[0] = static_cast<std::uint8_t>(value);
    p[1] = static_cast<std::uint8_t>(value >> 8);
}

void write_u32_le(std::uint8_t* p, std::uint32_t value)
{
    write_u16_le(p, static_cast<std::uint16_t>(value));
    write_u16_le(p + 2, static_cast<std::uint16_t>(value >> 16));
}

FrameBytes encode_request(MessageType type, std::uint32_t session, std::uint32_t sequence,
                          const std::uint8_t* payload, std::size_t size)
{
    FrameBytes output{};
    build_frame(type, session, sequence, payload, size, output);
    return output;
}

bool valid_result(std::uint8_t value) { return value <= static_cast<std::uint8_t>(ResultCode::CalibrationRejected); }
bool valid_state(std::uint8_t value) { return value <= static_cast<std::uint8_t>(ProbeState::Fault); }

vhm::Quality decode_quality(std::uint16_t flags)
{
    vhm::Quality quality = vhm::Quality::None;
    if ((flags & 0x0001U) != 0) quality |= vhm::Quality::TimeUncertain;
    if ((flags & 0x0002U) != 0) quality |= vhm::Quality::SegmentStart;
    if ((flags & 0x0004U) != 0) quality |= vhm::Quality::Saturated;
    return quality;
}

}  // namespace

DecodeError decode_stream(const Frame& frame, vhm::SampleBatch& output)
{
    const bool adxl = frame.type == MessageType::StreamAdxl;
    if (!adxl && frame.type != MessageType::StreamIcm) return DecodeError::WrongType;
    if (frame.payload_size < 32) return DecodeError::WrongLength;

    const std::uint8_t* p = frame.payload.data();
    const std::uint16_t count = read_u16_le(p + 28);
    const std::size_t width = adxl ? 9U : 13U;
    const std::size_t stream_limit = adxl ? 108U : 74U;
    if (count == 0 || count > stream_limit || count > vhm::kMaxSamplesPerBatch)
        return DecodeError::TooManySamples;
    if (frame.payload_size != 32U + count * width) return DecodeError::WrongLength;

    output = {};
    output.stream = adxl ? vhm::StreamId::Adxl : vhm::StreamId::Icm;
    output.format = adxl ? vhm::SampleFormat::Adxl20 : vhm::SampleFormat::Icm16;
    output.config_id = read_u32_le(p);
    output.segment = read_u32_le(p + 4);
    output.first_sequence = read_u64_le(p + 8);
    output.first_time_us = read_u64_le(p + 16);
    output.period_q16 = read_u32_le(p + 24);
    output.count = count;
    output.quality = decode_quality(read_u16_le(p + 30));

    for (std::size_t sample = 0; sample < count; ++sample) {
        const std::uint8_t* raw = p + 32 + sample * width;
        if (adxl) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const std::uint8_t* word = raw + axis * 3;
                if ((word[2] & 0xF0U) != 0) output.quality |= vhm::Quality::FormatError;
                std::uint32_t value = static_cast<std::uint32_t>(word[0]) |
                                      (static_cast<std::uint32_t>(word[1]) << 8) |
                                      ((static_cast<std::uint32_t>(word[2]) & 0x0FU) << 16);
                output.samples[sample].channel[axis] =
                    (value & 0x80000U) ? static_cast<std::int32_t>(value - 0x100000U)
                                       : static_cast<std::int32_t>(value);
            }
        } else {
            for (std::size_t axis = 0; axis < 6; ++axis)
                output.samples[sample].channel[axis] = read_i16(raw + axis * 2);
            output.samples[sample].temperature_raw = static_cast<std::int8_t>(raw[12]);
        }
    }
    return DecodeError::None;
}

DecodeError decode_attitude(const Frame& frame, AttitudeSample& output)
{
    if (frame.type != MessageType::Attitude) return DecodeError::WrongType;
    if (frame.payload_size != 48) return DecodeError::WrongLength;
    const std::uint8_t* p = frame.payload.data();
    if (read_u16_le(p + 28) != 1 || read_u32_le(p + 24) != (5000U << 16) ||
        (read_u16_le(p + 30) & 0xFFFEU) != 0)
        return DecodeError::InvalidField;
    output = {};
    output.config_id = read_u32_le(p);
    output.segment = read_u32_le(p + 4);
    output.sequence = read_u64_le(p + 8);
    output.time_us = read_u64_le(p + 16);
    output.period_q16 = read_u32_le(p + 24);
    if ((read_u16_le(p + 30) & 1U) != 0) output.quality = vhm::Quality::TimeUncertain;
    for (std::size_t i = 0; i < output.quat_q30.size(); ++i)
        output.quat_q30[i] = read_i32(p + 32 + i * 4);
    return DecodeError::None;
}

DecodeError decode_calibration_record(const Frame& frame, CalibrationRecord& output)
{
    if (frame.type != MessageType::SaveCalibration) return DecodeError::WrongType;
    if (frame.payload_size != 81) return DecodeError::WrongLength;
    const std::uint8_t* p = frame.payload.data();
    if (p[7] != 0 || p[8] != 0) return DecodeError::InvalidField;
    output = {};
    if (!valid_result(p[4])) return DecodeError::InvalidField;
    output.request_id = read_u32_le(p);
    output.result = static_cast<ResultCode>(p[4]); output.version = p[5]; output.valid_flags = p[6];
    output.generation = read_u32_le(p + 9); output.created_ms = read_u32_le(p + 13);
    std::array<float*, 5> groups{{output.adxl_accel_bias_g.data(),
                                  output.adxl_accel_scale.data(),
                                  output.icm_accel_bias_g.data(),
                                  output.icm_accel_scale.data(),
                                  output.icm_gyro_bias_dps.data()}};
    std::size_t offset = 17;
    for (float* group : groups)
        for (std::size_t axis = 0; axis < 3; ++axis, offset += 4)
            group[axis] = read_float(p + offset);
    output.temperature_c = read_float(p + 77);
    return DecodeError::None;
}

FrameBytes encode_hello_request(std::uint32_t sequence, std::uint32_t request, std::uint32_t new_session)
{
    std::uint8_t payload[8]{};
    write_u32_le(payload, request);
    write_u32_le(payload + 4, new_session);
    return encode_request(MessageType::Hello, 0, sequence, payload, sizeof(payload));
}
FrameBytes encode_set_link_request(std::uint32_t session, std::uint32_t sequence, std::uint32_t request, std::uint32_t baud)
{
    std::uint8_t payload[8]{};
    write_u32_le(payload, request);
    write_u32_le(payload + 4, baud);
    return encode_request(MessageType::SetLink, session, sequence, payload, sizeof(payload));
}
FrameBytes encode_set_config_request(std::uint32_t session, std::uint32_t sequence, std::uint32_t request, std::uint32_t config, const StreamConfig* streams, std::uint8_t count)
{
    if (streams == nullptr || count == 0 || count > 2 || config == 0) return {};
    std::uint8_t payload[17]{};
    write_u32_le(payload, request);
    write_u32_le(payload + 4, config);
    payload[8] = count;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t offset = 9 + i * 4;
        payload[offset] = streams[i].stream_id;
        payload[offset + 1] = streams[i].odr_code;
        payload[offset + 2] = streams[i].range_code;
        payload[offset + 3] = streams[i].filter_code;
    }
    return encode_request(MessageType::SetConfig, session, sequence, payload, 9U + count * 4U);
}
FrameBytes encode_start_request(std::uint32_t s, std::uint32_t q, std::uint32_t r) { std::uint8_t p[4]; write_u32_le(p, r); return encode_request(MessageType::Start, s, q, p, 4); }
FrameBytes encode_fetch_request(std::uint32_t s, std::uint32_t q, std::uint32_t r, std::uint32_t budget) { std::uint8_t p[8]; write_u32_le(p, r); write_u32_le(p + 4, budget); return encode_request(MessageType::Fetch, s, q, p, 8); }
FrameBytes encode_stop_request(std::uint32_t s, std::uint32_t q, std::uint32_t r) { std::uint8_t p[4]; write_u32_le(p, r); return encode_request(MessageType::Stop, s, q, p, 4); }
FrameBytes encode_get_status_request(std::uint32_t s, std::uint32_t q, std::uint32_t r) { std::uint8_t p[4]; write_u32_le(p, r); return encode_request(MessageType::GetStatus, s, q, p, 4); }
FrameBytes encode_calibrate_request(std::uint32_t s, std::uint32_t q, std::uint32_t r, std::uint8_t action, std::uint8_t target, std::uint8_t face) { std::uint8_t p[8]{}; write_u32_le(p, r); p[4]=action; p[5]=target; p[6]=face; return encode_request(MessageType::Calibrate, s, q, p, 8); }
FrameBytes encode_save_calibration_request(std::uint32_t s, std::uint32_t q, std::uint32_t r, std::uint8_t action) { std::uint8_t p[8]{}; write_u32_le(p, r); p[4]=action; return encode_request(MessageType::SaveCalibration, s, q, p, 8); }

DecodeError decode_result(const Frame& frame, MessageType type, Result& out)
{
    if (frame.type != type) return DecodeError::WrongType;
    if (frame.payload_size < 5) return DecodeError::WrongLength;
    if (!valid_result(frame.payload[4])) return DecodeError::InvalidField;
    out = {read_u32_le(frame.payload.data()), static_cast<ResultCode>(frame.payload[4])};
    return DecodeError::None;
}

DecodeError decode_hello(const Frame& f, HelloResponse& o)
{
    if (f.type != MessageType::Hello) return DecodeError::WrongType;
    if (f.payload_size != 29) return DecodeError::WrongLength;
    if (!valid_result(f.payload[4]) || !valid_state(f.payload[24]) || f.payload[25] || f.payload[26] || f.payload[27] || f.payload[28]) return DecodeError::InvalidField;
    o = {};
    o.result = {read_u32_le(f.payload.data()), static_cast<ResultCode>(f.payload[4])};
    o.probe_type = read_u16_le(f.payload.data() + 5);
    o.hardware_version = f.payload[7];
    o.firmware_major = f.payload[8];
    o.firmware_minor = f.payload[9];
    o.firmware_build = read_u16_le(f.payload.data() + 10);
    o.uptime_ms = read_u32_le(f.payload.data() + 12);
    o.supported_baud_mask = read_u16_le(f.payload.data() + 16);
    o.sensor_mask = read_u16_le(f.payload.data() + 18);
    o.config_id = read_u32_le(f.payload.data() + 20);
    o.state = static_cast<ProbeState>(f.payload[24]);
    return DecodeError::None;
}

DecodeError decode_set_config(const Frame& f, ConfigResponse& o)
{
    if (f.type != MessageType::SetConfig) return DecodeError::WrongType;
    if (f.payload_size != 5 && f.payload_size != 6 && f.payload_size != 10 && f.payload_size != 14)
        return DecodeError::WrongLength;
    if (!valid_result(f.payload[4])) return DecodeError::InvalidField;
    o = {};
    o.result = {read_u32_le(f.payload.data()), static_cast<ResultCode>(f.payload[4])};
    if (f.payload_size == 6) {
        o.rejected_stream = f.payload[5];
        return DecodeError::None;
    }
    if (f.payload_size == 5) return DecodeError::None;
    o.stream_count = f.payload[5];
    if (o.stream_count < 1 || o.stream_count > 2 || f.payload_size != 6U + o.stream_count * 4U)
        return DecodeError::InvalidField;
    for (std::size_t i = 0; i < o.stream_count; ++i) {
        const std::size_t offset = 6 + i * 4;
        o.streams[i] = {f.payload[offset], f.payload[offset + 1], f.payload[offset + 2], f.payload[offset + 3]};
    }
    return DecodeError::None;
}

DecodeError decode_stop(const Frame& f, StopResponse& o)
{
    if (f.type != MessageType::Stop) return DecodeError::WrongType;
    if (f.payload_size != 29) return DecodeError::WrongLength;
    if (!valid_result(f.payload[4])) return DecodeError::InvalidField;
    o = {};
    o.result = {read_u32_le(f.payload.data()), static_cast<ResultCode>(f.payload[4])};
    for (std::size_t i = 0; i < 2; ++i) {
        const std::size_t offset = 5 + i * 12;
        if (f.payload[offset + 1] || f.payload[offset + 2] || f.payload[offset + 3])
            return DecodeError::InvalidField;
        o.streams[i] = {f.payload[offset], read_u64_le(f.payload.data() + offset + 4)};
    }
    return DecodeError::None;
}

DecodeError decode_status(const Frame& f, ProbeStatus& o)
{
    if (f.type != MessageType::GetStatus) return DecodeError::WrongType;
    if (f.payload_size != 115) return DecodeError::WrongLength;
    if (!valid_state(f.payload[4]) || !valid_result(f.payload[111])) return DecodeError::InvalidField;

    o = {};
    o.uptime_ms = read_u32_le(f.payload.data());
    o.state = static_cast<ProbeState>(f.payload[4]);
    o.fault_bits = read_u16_le(f.payload.data() + 5);
    o.config_id = read_u32_le(f.payload.data() + 7);
    for (std::size_t i = 0; i < 2; ++i) {
        const std::size_t offset = 11 + i * 42;
        auto& stream = o.streams[i];
        if (f.payload[offset + 1] || f.payload[offset + 2] || f.payload[offset + 3])
            return DecodeError::InvalidField;
        stream.stream_id = f.payload[offset];
        stream.total_samples = read_u64_le(f.payload.data() + offset + 4);
        stream.segment = read_u32_le(f.payload.data() + offset + 12);
        stream.sequence_gaps = read_u32_le(f.payload.data() + offset + 16);
        stream.queued_batches = read_u16_le(f.payload.data() + offset + 20);
        stream.queued_bytes = read_u32_le(f.payload.data() + offset + 22);
        stream.spi_errors = read_u32_le(f.payload.data() + offset + 26);
        stream.fifo_overflows = read_u32_le(f.payload.data() + offset + 30);
        stream.format_time_errors = read_u32_le(f.payload.data() + offset + 34);
        stream.queue_drops = read_u32_le(f.payload.data() + offset + 38);
    }
    o.bad_types = read_u32_le(f.payload.data() + 95);
    o.bad_sessions = read_u32_le(f.payload.data() + 99);
    o.busy_rejects = read_u32_le(f.payload.data() + 103);
    o.crc_errors = read_u32_le(f.payload.data() + 107);
    o.last_result = static_cast<ResultCode>(f.payload[111]);
    if (f.payload[112] || f.payload[113] || f.payload[114]) return DecodeError::InvalidField;
    return DecodeError::None;
}

DecodeError decode_temperature(const Frame& f, TemperatureSample& o)
{
    if (f.type != MessageType::Temperature) return DecodeError::WrongType;
    if (f.payload_size != 16) return DecodeError::WrongLength;
    const auto raw = read_u16_le(f.payload.data() + 12);
    if (raw > 4095 || (f.payload[14] & ~3U) || f.payload[15] ||
        ((f.payload[14] & 2U) && !(f.payload[14] & 1U))) return DecodeError::InvalidField;
    o = {read_u32_le(f.payload.data()), read_u64_le(f.payload.data() + 4), raw,
         (f.payload[14] & 1U) != 0, (f.payload[14] & 2U) != 0};
    return DecodeError::None;
}

DecodeError decode_fetch_done(const Frame& f, FetchDone& o)
{
    if (f.type != MessageType::FetchDone) return DecodeError::WrongType;
    if (f.payload_size != 27) return DecodeError::WrongLength;
    if (!valid_result(f.payload[4]) || !valid_state(f.payload[23]) || f.payload[24] ||
        f.payload[25] || f.payload[26])
        return DecodeError::InvalidField;
    o = {};
    o.result = {read_u32_le(f.payload.data()), static_cast<ResultCode>(f.payload[4])};
    o.bytes_sent = read_u32_le(f.payload.data() + 5);
    o.frames_sent = read_u16_le(f.payload.data() + 9);
    for (std::size_t i = 0; i < 2; ++i) {
        const std::size_t offset = 11 + i * 6;
        o.backlog[i] = {read_u16_le(f.payload.data() + offset),
                        read_u32_le(f.payload.data() + offset + 2)};
    }
    o.state = static_cast<ProbeState>(f.payload[23]);
    return DecodeError::None;
}

DecodeError decode_calibration_status(const Frame& f, CalibrationStatus& o)
{
    if (f.type != MessageType::Calibrate) return DecodeError::WrongType;
    if (f.payload_size != 16) return DecodeError::WrongLength;
    if (!valid_result(f.payload[4]) || !valid_result(f.payload[11]))
        return DecodeError::InvalidField;
    o = {};
    o.result = {read_u32_le(f.payload.data()), static_cast<ResultCode>(f.payload[4])};
    o.state = f.payload[5];
    o.target = f.payload[6];
    o.face = f.payload[7];
    o.completed_faces = f.payload[8];
    o.valid_candidates = f.payload[9];
    o.candidate_modified = f.payload[10];
    o.window_result = static_cast<ResultCode>(f.payload[11]);
    o.sample_count = read_u32_le(f.payload.data() + 12);
    return DecodeError::None;
}

}  // namespace vhm::probe
