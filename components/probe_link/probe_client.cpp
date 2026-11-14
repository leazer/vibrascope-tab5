/**
 * @file probe_client.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-09-10
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "vhm/probe/probe_client.hpp"

#include <algorithm>

namespace vhm::probe {
namespace {

std::uint32_t response_request_id(const Frame& frame)
{
    if (frame.payload_size < 4 || frame.type == MessageType::GetStatus ||
        frame.type == MessageType::StreamAdxl || frame.type == MessageType::StreamIcm ||
        frame.type == MessageType::TimeInfo || frame.type == MessageType::Status ||
        frame.type == MessageType::Attitude || frame.type == MessageType::Temperature)
        return 0;
    const auto* p = frame.payload.data();
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

ProbeClient::ProbeClient(IByteTransport& transport, ClientTiming timing)
    : transport_(transport), timing_(timing)
{
    context_.baud = timing_.fallback_baud;
    context_.counters.baud = timing_.fallback_baud;
}

LinkSnapshot ProbeClient::snapshot() const
{
    LinkSnapshot result = context_.counters;
    result.phase = context_.phase;
    result.baud = context_.baud;
    result.crc_errors = parser_.crc_errors();
    return result;
}

bool ProbeClient::send_with_retry(const FrameBytes& request, MessageType response_type,
                                  Frame& response, IProbeEventSink* sink)
{
    for (std::uint32_t attempt = 0; attempt <= timing_.retries; ++attempt) {
        if (transport_.write(request.bytes.data(), request.size) != request.size) {
            context_.phase = LinkPhase::Fault;
            return false;
        }
        const auto deadline = transport_.now_ms() + timing_.response_timeout_ms;
        if (consume_until(deadline, response_type, response, sink)) return true;
        ++context_.counters.timeouts;
        if (attempt < timing_.retries) ++context_.counters.retries;
    }
    return false;
}

bool ProbeClient::exchange(const FrameBytes& request, MessageType response_type,
                           Frame& response, IProbeEventSink* sink)
{
    if (request.size == 0) return false;
    awaited_request_id_ = context_.request_id;
    return send_with_retry(request, response_type, response, sink);
}

bool ProbeClient::consume_until(std::uint64_t deadline_ms, MessageType response_type,
                                Frame& response, IProbeEventSink* sink)
{
    awaited_type_ = response_type;
    awaited_frame_ = &response;
    event_sink_ = sink;
    response_ready_ = false;
    std::array<std::uint8_t, 256> bytes{};
    while (!response_ready_ && transport_.now_ms() < deadline_ms) {
        const auto remaining = static_cast<std::uint32_t>(deadline_ms - transport_.now_ms());
        const std::size_t count = transport_.read(bytes.data(), bytes.size(), remaining);
        if (count != 0) parser_.feed(bytes.data(), count, *this);
    }
    awaited_frame_ = nullptr;
    event_sink_ = nullptr;
    return response_ready_;
}

void ProbeClient::on_frame(const Frame& frame)
{
    if (frame.session != context_.session_id) {
        ++context_.counters.bad_sessions;
        return;
    }
    if (event_sink_ != nullptr && (frame.type == MessageType::StreamAdxl ||
        frame.type == MessageType::StreamIcm || frame.type == MessageType::TimeInfo ||
        frame.type == MessageType::Status || frame.type == MessageType::Attitude ||
        frame.type == MessageType::Temperature)) {
        dispatch_stream(frame, *event_sink_);
        return;
    }
    if (awaited_frame_ == nullptr || frame.type != awaited_type_) return;
    const std::uint32_t request = response_request_id(frame);
    if (request != 0 && request != awaited_request_id_) return;
    *awaited_frame_ = frame;
    response_ready_ = true;
}

void ProbeClient::dispatch_stream(const Frame& frame, IProbeEventSink& sink)
{
    if (frame.type == MessageType::StreamAdxl || frame.type == MessageType::StreamIcm) {
        // Standby FETCH is pose-only. Ignore stray raw frames before sequence
        // tracking and consumer delivery; STOP drain remains a measurement path.
        if (context_.phase != LinkPhase::Measuring && context_.phase != LinkPhase::Stopping) return;
        vhm::SampleBatch batch{};
        if (decode_stream(frame, batch) != DecodeError::None) return;
        const std::size_t index = batch.stream == vhm::StreamId::Adxl ? 0 : 1;
        if (context_.expected_sequence[index] != 0 &&
            batch.first_sequence != context_.expected_sequence[index])
            ++context_.counters.sequence_gaps;
        context_.expected_sequence[index] = batch.first_sequence + batch.count;
        sink.on_batch(batch);
    } else if (frame.type == MessageType::Attitude) {
        AttitudeSample sample{};
        if (decode_attitude(frame, sample) == DecodeError::None) sink.on_attitude(sample);
    } else if (frame.type == MessageType::Temperature) {
        TemperatureSample sample{};
        if (decode_temperature(frame, sample) == DecodeError::None) sink.on_temperature(sample);
    } else if (frame.type == MessageType::TimeInfo) {
        sink.on_time_info(frame);
    } else if (frame.type == MessageType::Status) {
        sink.on_status(frame);
    }
}

bool ProbeClient::connect(std::uint32_t new_session)
{
    if (new_session == 0) return false;
    context_.session_id = new_session;
    const auto request_id = context_.request_id++;
    const auto request = encode_hello_request(context_.frame_sequence++, request_id, new_session);
    context_.request_id = request_id;
    Frame frame{};
    const bool received = exchange(request, MessageType::Hello, frame);
    context_.request_id = request_id + 1;
    HelloResponse hello{};
    if (!received || decode_hello(frame, hello) != DecodeError::None ||
        hello.result.request_id != request_id || hello.result.code != ResultCode::Ok) {
        context_.phase = LinkPhase::Disconnected;
        return false;
    }
    context_.phase = LinkPhase::Standby;
    return true;
}

bool ProbeClient::set_link(std::uint32_t baud)
{
    if (context_.phase == LinkPhase::Disconnected || baud == 0) return false;
    const auto request_id = context_.request_id++;
    const auto request = encode_set_link_request(context_.session_id, context_.frame_sequence++, request_id, baud);
    context_.request_id = request_id;
    Frame frame{};
    Result result{};
    const bool accepted = exchange(request, MessageType::SetLink, frame) &&
                          decode_result(frame, MessageType::SetLink, result) == DecodeError::None &&
                          result.request_id == request_id && result.code == ResultCode::Ok;
    context_.request_id = request_id + 1;
    if (!accepted || !transport_.set_baud(baud)) return false;
    context_.baud = baud;
    ProbeStatus status{};
    if (get_status(status)) return true;

    transport_.set_baud(timing_.fallback_baud);
    context_.baud = timing_.fallback_baud;
    parser_.reset();
    context_.phase = LinkPhase::Disconnected;
    std::uint32_t fallback_session = context_.session_id + 1;
    if (fallback_session == 0) fallback_session = 1;
    connect(fallback_session);
    return false;
}

bool ProbeClient::configure(std::uint32_t config_id, const StreamConfig* streams, std::uint8_t count)
{
    if (context_.phase != LinkPhase::Standby) return false;
    const auto request_id = context_.request_id++;
    const auto request = encode_set_config_request(context_.session_id, context_.frame_sequence++, request_id, config_id, streams, count);
    context_.request_id = request_id;
    Frame frame{};
    ConfigResponse response{};
    const bool ok = exchange(request, MessageType::SetConfig, frame) &&
                    decode_set_config(frame, response) == DecodeError::None &&
                    response.result.request_id == request_id && response.result.code == ResultCode::Ok;
    context_.request_id = request_id + 1;
    return ok;
}

bool ProbeClient::start()
{
    if (context_.phase != LinkPhase::Standby) return false;
    const auto request_id = context_.request_id++;
    const auto request = encode_start_request(context_.session_id, context_.frame_sequence++, request_id);
    context_.request_id = request_id;
    Frame frame{};
    Result result{};
    const bool ok = exchange(request, MessageType::Start, frame) &&
                    decode_result(frame, MessageType::Start, result) == DecodeError::None &&
                    result.request_id == request_id && result.code == ResultCode::Ok;
    context_.request_id = request_id + 1;
    if (ok) context_.phase = LinkPhase::Measuring;
    return ok;
}

bool ProbeClient::fetch(std::uint32_t byte_budget, IProbeEventSink& sink, FetchDone* fetch_done)
{
    if (context_.phase != LinkPhase::Standby && context_.phase != LinkPhase::Measuring &&
        context_.phase != LinkPhase::Stopping) return false;
    const auto request_id = context_.request_id++;
    const auto request = encode_fetch_request(context_.session_id, context_.frame_sequence++, request_id, byte_budget);
    context_.request_id = request_id;
    Frame frame{};
    FetchDone done{};
    const bool ok = exchange(request, MessageType::FetchDone, frame, &sink) &&
                    decode_fetch_done(frame, done) == DecodeError::None &&
                    done.result.request_id == request_id && done.result.code == ResultCode::Ok;
    context_.request_id = request_id + 1;
    if (ok && fetch_done != nullptr) *fetch_done = done;
    if (ok && context_.phase == LinkPhase::Stopping) {
        bool drained = true;
        for (std::size_t i = 0; i < 2; ++i) {
            if (context_.boundary_valid[i] && context_.expected_sequence[i] <= context_.stop_boundaries[i])
                drained = false;
        }
        if (drained) context_.phase = LinkPhase::Standby;
    }
    return ok;
}

bool ProbeClient::stop(StopResponse& response)
{
    if (context_.phase != LinkPhase::Measuring) return false;
    context_.phase = LinkPhase::Stopping;
    const auto request_id = context_.request_id++;
    const auto request = encode_stop_request(context_.session_id, context_.frame_sequence++, request_id);
    context_.request_id = request_id;
    Frame frame{};
    const bool ok = exchange(request, MessageType::Stop, frame) &&
                    decode_stop(frame, response) == DecodeError::None &&
                    response.result.request_id == request_id && response.result.code == ResultCode::Ok;
    context_.request_id = request_id + 1;
    if (ok) {
        for (const auto& boundary : response.streams) {
            const std::size_t index = boundary.stream_id == 1 ? 0 : 1;
            context_.stop_boundaries[index] = boundary.last_sequence;
            context_.boundary_valid[index] = boundary.stream_id == 1 || boundary.stream_id == 2;
        }
    }
    return ok;
}

bool ProbeClient::get_status(ProbeStatus& status)
{
    if (context_.phase == LinkPhase::Disconnected) return false;
    const auto request_id = context_.request_id++;
    const auto request = encode_get_status_request(context_.session_id, context_.frame_sequence++, request_id);
    context_.request_id = request_id;
    Frame frame{};
    const bool ok = exchange(request, MessageType::GetStatus, frame) &&
                    decode_status(frame, status) == DecodeError::None;
    context_.request_id = request_id + 1;
    return ok;
}

bool ProbeClient::calibrate(std::uint8_t action, std::uint8_t target, std::uint8_t face,
                            CalibrationStatus& status)
{
    const auto request_id = context_.request_id++;
    const auto request = encode_calibrate_request(context_.session_id, context_.frame_sequence++, request_id, action, target, face);
    context_.request_id = request_id;
    Frame frame{};
    const bool ok = exchange(request, MessageType::Calibrate, frame) &&
                    decode_calibration_status(frame, status) == DecodeError::None &&
                    status.result.request_id == request_id;
    context_.request_id = request_id + 1;
    return ok;
}

bool ProbeClient::save_calibration(std::uint8_t action, CalibrationRecord& record)
{
    const auto request_id = context_.request_id++;
    const auto request = encode_save_calibration_request(context_.session_id, context_.frame_sequence++, request_id, action);
    context_.request_id = request_id;
    Frame frame{};
    const bool ok = exchange(request, MessageType::SaveCalibration, frame) &&
                    decode_calibration_record(frame, record) == DecodeError::None &&
                    record.request_id == request_id;
    context_.request_id = request_id + 1;
    return ok;
}

}  // namespace vhm::probe
