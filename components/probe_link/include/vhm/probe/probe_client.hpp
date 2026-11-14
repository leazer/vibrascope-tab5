/**
 * @file probe_client.hpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-09-10
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <array>
#include <cstdint>

#include "vhm/probe/byte_transport.hpp"
#include "vhm/probe/messages.hpp"

namespace vhm::probe {

enum class LinkPhase : std::uint8_t { Disconnected, Standby, Measuring, Stopping, Fault };

struct ClientTiming {
    std::uint32_t response_timeout_ms{100};
    std::uint32_t retries{2};
    std::uint32_t fallback_baud{1000000};
};

struct LinkSnapshot {
    LinkPhase phase{LinkPhase::Disconnected};
    std::uint32_t baud{1000000};
    std::uint32_t crc_errors{0};
    std::uint32_t timeouts{0};
    std::uint32_t retries{0};
    std::uint32_t bad_sessions{0};
    std::uint32_t sequence_gaps{0};
};

class IProbeEventSink {
public:
    virtual ~IProbeEventSink() = default;
    virtual void on_batch(const vhm::SampleBatch&) {}
    virtual void on_attitude(const AttitudeSample&) {}
    virtual void on_temperature(const TemperatureSample&) {}
    virtual void on_time_info(const Frame&) {}
    virtual void on_status(const Frame&) {}
};

class ProbeClient final : private IFrameSink {
public:
    explicit ProbeClient(IByteTransport& transport, ClientTiming timing = {});

    bool connect(std::uint32_t new_session);
    bool set_link(std::uint32_t baud);
    bool configure(std::uint32_t config_id, const StreamConfig* streams, std::uint8_t count);
    bool start();
    // Standby delivers attitude/status only; Measuring and Stopping also deliver raw batches.
    bool fetch(std::uint32_t byte_budget, IProbeEventSink& sink,
               FetchDone* fetch_done = nullptr);
    bool stop(StopResponse& response);
    bool get_status(ProbeStatus& status);
    bool calibrate(std::uint8_t action, std::uint8_t target, std::uint8_t face,
                   CalibrationStatus& status);
    bool save_calibration(std::uint8_t action, CalibrationRecord& record);

    LinkSnapshot snapshot() const;
    std::uint32_t session_id() const { return context_.session_id; }

private:
    struct Context {
        LinkPhase phase{LinkPhase::Disconnected};
        std::uint32_t session_id{0};
        std::uint32_t request_id{1};
        std::uint32_t frame_sequence{1};
        std::uint32_t baud{1000000};
        std::array<std::uint64_t, 2> expected_sequence{};
        std::array<std::uint64_t, 2> stop_boundaries{};
        std::array<bool, 2> boundary_valid{};
        LinkSnapshot counters{};
    };

    bool exchange(const FrameBytes& request, MessageType response_type,
                  Frame& response, IProbeEventSink* sink = nullptr);
    bool send_with_retry(const FrameBytes& request, MessageType response_type,
                         Frame& response, IProbeEventSink* sink);
    bool consume_until(std::uint64_t deadline_ms, MessageType response_type,
                       Frame& response, IProbeEventSink* sink);
    void dispatch_stream(const Frame& frame, IProbeEventSink& sink);
    void on_frame(const Frame& frame) override;

    IByteTransport& transport_;
    ClientTiming timing_;
    Context context_{};
    MessageType awaited_type_{MessageType::Hello};
    Frame* awaited_frame_{nullptr};
    IProbeEventSink* event_sink_{nullptr};
    bool response_ready_{false};
    std::uint32_t awaited_request_id_{0};
    FrameParser parser_{};
};

}  // namespace vhm::probe
