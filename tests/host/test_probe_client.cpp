/**
 * @file test_probe_client.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-09-10
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "fake_byte_transport.hpp"
#include "test_support.hpp"
#include "vhm/probe/frame_codec.hpp"
#include "vhm/probe/probe_client.hpp"
#include "../../app/apps/app_vibescope/scope_temperature.h"

#include <cstring>

namespace {

std::uint32_t read32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

vhm::probe::FrameBytes hello_response(std::uint32_t session, std::uint32_t request)
{
    std::uint8_t payload[29]{};
    std::memcpy(payload, &request, sizeof(request));
    payload[4] = 0;
    payload[5] = 1;
    payload[16] = 0x0F;
    payload[18] = 0x03;
    payload[24] = 1;
    vhm::probe::FrameBytes frame{};
    vhm::probe::build_frame(vhm::probe::MessageType::Hello, session, 7,
                            payload, sizeof(payload), frame);
    return frame;
}

vhm::probe::FrameBytes result_response(vhm::probe::MessageType type, std::uint32_t session,
                                       std::uint32_t request)
{
    std::uint8_t payload[5]{};
    std::memcpy(payload, &request, sizeof(request));
    vhm::probe::FrameBytes frame{};
    vhm::probe::build_frame(type, session, 8, payload, sizeof(payload), frame);
    return frame;
}

vhm::probe::FrameBytes status_response(std::uint32_t session)
{
    std::uint8_t payload[115]{};
    payload[4] = 1;
    payload[11] = 1;
    payload[53] = 2;
    vhm::probe::FrameBytes frame{};
    vhm::probe::build_frame(vhm::probe::MessageType::GetStatus, session, 9,
                            payload, sizeof(payload), frame);
    return frame;
}

void test_connect_and_retry_same_request()
{
    FakeByteTransport transport;
    int attempts = 0;
    transport.fragment_size = 1;
    transport.on_write = [&](const std::vector<std::uint8_t>& bytes, FakeByteTransport& fake) {
        ++attempts;
        if (attempts == 2) fake.enqueue(hello_response(0x12345678, read32(bytes.data() + 15)));
    };
    vhm::probe::ProbeClient client(transport, {10, 2, 1000000});
    EXPECT_TRUE(client.connect(0x12345678));
    EXPECT_TRUE(client.session_id() == 0x12345678);
    EXPECT_TRUE(client.snapshot().phase == vhm::probe::LinkPhase::Standby);
    EXPECT_TRUE(transport.writes.size() == 2);
    EXPECT_TRUE(transport.writes[0] == transport.writes[1]);
    EXPECT_TRUE(client.snapshot().timeouts == 1);
    EXPECT_TRUE(client.snapshot().retries == 1);
}

void test_wrong_session_is_ignored()
{
    FakeByteTransport transport;
    transport.on_write = [&](const std::vector<std::uint8_t>& bytes, FakeByteTransport& fake) {
        const auto request = read32(bytes.data() + 15);
        fake.enqueue(hello_response(3, request));
    };
    vhm::probe::ProbeClient client(transport, {5, 0, 1000000});
    EXPECT_FALSE(client.connect(4));
    EXPECT_TRUE(client.snapshot().bad_sessions == 1);
}

void test_set_link_changes_local_baud_after_acceptance()
{
    FakeByteTransport transport;
    std::uint32_t session = 9;
    transport.on_write = [&](const std::vector<std::uint8_t>& bytes, FakeByteTransport& fake) {
        const auto type = static_cast<vhm::probe::MessageType>(bytes[4]);
        const auto request = read32(bytes.data() + 15);
        if (type == vhm::probe::MessageType::Hello) fake.enqueue(hello_response(session, request));
        else if (type == vhm::probe::MessageType::GetStatus) fake.enqueue(status_response(session));
        else fake.enqueue(result_response(type, session, request));
    };
    vhm::probe::ProbeClient client(transport);
    EXPECT_TRUE(client.connect(session));
    EXPECT_TRUE(client.set_link(5000000));
    EXPECT_TRUE(transport.baud_calls.size() == 1 && transport.baud_calls[0] == 5000000);
    EXPECT_TRUE(client.snapshot().baud == 5000000);
}

void test_unconfirmed_high_speed_falls_back_and_reconnects()
{
    FakeByteTransport transport;
    const std::uint32_t session = 11;
    transport.on_write = [&](const std::vector<std::uint8_t>& bytes, FakeByteTransport& fake) {
        const auto type = static_cast<vhm::probe::MessageType>(bytes[4]);
        const auto request = read32(bytes.data() + 15);
        if (type == vhm::probe::MessageType::Hello)
            fake.enqueue(hello_response(read32(bytes.data() + 19), request));
        else if (type == vhm::probe::MessageType::SetLink)
            fake.enqueue(result_response(type, session, request));
    };
    vhm::probe::ProbeClient client(transport, {5, 0, 1000000});
    EXPECT_TRUE(client.connect(session));
    EXPECT_FALSE(client.set_link(5000000));
    EXPECT_TRUE(transport.baud_calls.size() == 2);
    EXPECT_TRUE(transport.baud_calls[0] == 5000000 && transport.baud_calls[1] == 1000000);
    EXPECT_TRUE(client.snapshot().baud == 1000000);
    EXPECT_TRUE(client.snapshot().phase == vhm::probe::LinkPhase::Standby);
}

// Exercise the wire path through HELLO, standby FETCH, measurement and STOP drain.
void test_standby_fetch_pose_and_stop_drain()
{
    using namespace vhm::probe;
    struct Sink final : IProbeEventSink {
        unsigned batches{}, poses{}, temperatures{};
        void on_temperature(const TemperatureSample& sample) override {
            ++temperatures;
            EXPECT_TRUE(sample.raw == 1885 && sample.valid);
        }
        void on_batch(const vhm::SampleBatch&) override { ++batches; }
        void on_attitude(const AttitudeSample& sample) override
        {
            ++poses;
            EXPECT_TRUE(sample.quat_q30[0] == (1 << 30));
        }
    } sink;
    FakeByteTransport transport;
    transport.fragment_size = 7;
    const std::uint32_t session = 31;
    bool send_pose = true;
    bool send_raw = true;
    unsigned raw_sequence = 100;
    auto enqueue = [&](FakeByteTransport& fake, MessageType type, const std::uint8_t* payload, std::size_t size) {
        FrameBytes frame{};
        build_frame(type, session, 1, payload, size, frame);
        fake.enqueue(frame);
    };
    transport.on_write = [&](const std::vector<std::uint8_t>& bytes, FakeByteTransport& fake) {
        const auto type = static_cast<MessageType>(bytes[4]);
        const auto request = read32(bytes.data() + 15);
        if (type == MessageType::Hello) fake.enqueue(hello_response(session, request));
        else if (type == MessageType::Fetch) {
            EXPECT_TRUE(read32(bytes.data() + 19) == 1024);
            // Raw received in standby must not reach consumers or sequence tracking.
            std::uint8_t raw[41]{};
            raw[8] = static_cast<std::uint8_t>(raw_sequence++);
            raw[28] = 1;
            if (send_raw) enqueue(fake, MessageType::StreamAdxl, raw, sizeof(raw));
            if (send_pose) {
                std::uint8_t pose[48]{};
                const std::uint32_t period = 5000U << 16, quaternion = 1U << 30;
                std::memcpy(pose + 24, &period, 4);
                std::memcpy(pose + 32, &quaternion, 4);
                pose[28] = 1;
                enqueue(fake, MessageType::Attitude, pose, sizeof(pose));
                std::uint8_t temp[16]{};
                temp[12] = 0x5d; temp[13] = 7; temp[14] = 1;
                enqueue(fake, MessageType::Temperature, temp, sizeof(temp));
            }
            std::uint8_t done[27]{};
            std::memcpy(done, &request, 4);
            enqueue(fake, MessageType::FetchDone, done, sizeof(done));
        } else if (type == MessageType::Stop) {
            std::uint8_t stop[29]{};
            std::memcpy(stop, &request, 4);
            stop[5] = 1;
            stop[9] = static_cast<std::uint8_t>(raw_sequence + 1);
            enqueue(fake, MessageType::Stop, stop, sizeof(stop));
        } else fake.enqueue(result_response(type, session, request));
    };
    ProbeClient client(transport, {10, 0, 1000000});
    EXPECT_FALSE(client.fetch(1024, sink));
    EXPECT_TRUE(transport.writes.empty());
    EXPECT_TRUE(client.connect(session));
    FetchDone done{};
    EXPECT_TRUE(client.fetch(1024, sink, &done));
    EXPECT_TRUE(sink.poses == 1 && sink.batches == 0 && sink.temperatures == 1);
    EXPECT_TRUE(client.snapshot().phase == LinkPhase::Standby);
    send_pose = false;  // Older probe: empty standby window is successful too.
    send_raw = false;
    EXPECT_TRUE(client.fetch(1024, sink));
    EXPECT_TRUE(sink.poses == 1 && sink.batches == 0);
    raw_sequence = 1;
    send_raw = true;
    EXPECT_TRUE(client.start());
    EXPECT_TRUE(client.fetch(1024, sink));
    EXPECT_TRUE(sink.batches == 1 && client.snapshot().sequence_gaps == 0);
    StopResponse stopped{};
    EXPECT_TRUE(client.stop(stopped));
    EXPECT_TRUE(client.snapshot().phase == LinkPhase::Stopping);
    EXPECT_TRUE(client.fetch(1024, sink));
    EXPECT_TRUE(sink.batches == 2 && client.snapshot().phase == LinkPhase::Stopping);
    EXPECT_TRUE(client.fetch(1024, sink));
    EXPECT_TRUE(sink.batches == 3 && client.snapshot().phase == LinkPhase::Standby);
    send_pose = true;
    EXPECT_TRUE(client.fetch(1024, sink));
    EXPECT_TRUE(sink.batches == 3 && sink.poses == 2);
    EXPECT_TRUE(client.snapshot().sequence_gaps == 0);
    EXPECT_TRUE(client.snapshot().timeouts == 0 && client.snapshot().retries == 0);
}

}  // namespace

void run_probe_client_tests()
{
    {
        vibescope::TemperatureAge age;
        EXPECT_FALSE(age.fresh(0));
        age.observe(1000000, 1, 100);
        for (unsigned now = 1000; now <= 5000; now += 1000)
            age.observe(1000000, 1, now);  // Replayed cache cannot refresh age.
        EXPECT_FALSE(age.fresh(5000));
        age.observe(6000000, 1, 5000);
        EXPECT_TRUE(age.fresh(5000));
        age = {};  // Reconnect clears the prior sensor's clock.
        age.observe(1000, 2, 6000);
        EXPECT_TRUE(age.fresh(6000));
    }
    {
        using namespace vhm::probe;
        Frame f{};
        f.type = MessageType::Temperature;
        f.payload_size = 16;
        f.payload[12] = 0x5d; f.payload[13] = 0x07; f.payload[14] = 3;
        TemperatureSample sample{};
        EXPECT_TRUE(decode_temperature(f, sample) == DecodeError::None);
        EXPECT_TRUE(sample.raw == 1885 && sample.valid && sample.live);
        f.payload[15] = 1;
        EXPECT_TRUE(decode_temperature(f, sample) == DecodeError::InvalidField);
        f.payload[15] = 0; f.payload[13] = 0x10;
        EXPECT_TRUE(decode_temperature(f, sample) == DecodeError::InvalidField);
        f.payload_size = 15;
        EXPECT_TRUE(decode_temperature(f, sample) == DecodeError::WrongLength);
    }
    test_connect_and_retry_same_request();
    test_wrong_session_is_ignored();
    test_set_link_changes_local_baud_after_acceptance();
    test_unconfirmed_high_speed_falls_back_and_reconnects();
    test_standby_fetch_pose_and_stop_drain();
}
