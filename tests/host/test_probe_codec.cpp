#include "test_support.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "vhm/probe/frame_codec.hpp"
#include "vhm/probe/messages.hpp"

namespace {

constexpr std::array<std::uint8_t, 27> kHello{{
    0xA5, 0x5A, 0xC3, 0x3C, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x44, 0x33, 0x22,
    0x11, 0xBC, 0xF0, 0x6B, 0x2F,
}};

constexpr std::array<std::uint8_t, 60> kAdxl{{
    0xA5, 0x5A, 0xC3, 0x3C, 0x10, 0x29, 0x00, 0x44, 0x33, 0x22, 0x11,
    0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x42,
    0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFA, 0x00, 0x01,
    0x00, 0x03, 0x00, 0xD8, 0xF5, 0x0F, 0x8C, 0x0A, 0x00, 0x24, 0xC2,
    0x00, 0x18, 0x19, 0xC2, 0xBA,
}};

void put16(std::uint8_t* out, std::uint16_t value)
{
    out[0] = static_cast<std::uint8_t>(value);
    out[1] = static_cast<std::uint8_t>(value >> 8);
}

void put32(std::uint8_t* out, std::uint32_t value)
{
    put16(out, static_cast<std::uint16_t>(value));
    put16(out + 2, static_cast<std::uint16_t>(value >> 16));
}

void put64(std::uint8_t* out, std::uint64_t value)
{
    put32(out, static_cast<std::uint32_t>(value));
    put32(out + 4, static_cast<std::uint32_t>(value >> 32));
}

void put_float(std::uint8_t* out, float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    put32(out, bits);
}

class FrameCollector final : public vhm::probe::IFrameSink {
public:
    void on_frame(const vhm::probe::Frame& value) override
    {
        frame = value;
        ++count;
    }

    vhm::probe::Frame frame{};
    std::size_t count{0};
};

vhm::probe::Frame parse_one(const vhm::probe::FrameBytes& bytes)
{
    FrameCollector collector;
    vhm::probe::FrameParser parser;
    EXPECT_TRUE(parser.feed(bytes.bytes.data(), bytes.size, collector) == 1);
    return collector.frame;
}

}  // namespace

void run_probe_codec_tests()
{
    using namespace vhm::probe;

    EXPECT_TRUE(crc32c(reinterpret_cast<const std::uint8_t*>("123456789"), 9) ==
                0xE3069283U);

    FrameBytes built{};
    const std::uint8_t hello_payload[] = {1, 0, 0, 0, 0x44, 0x33, 0x22, 0x11};
    EXPECT_TRUE(build_frame(MessageType::Hello, 0, 0, hello_payload,
                            sizeof(hello_payload), built));
    EXPECT_TRUE(built.size == kHello.size());
    EXPECT_TRUE(std::memcmp(built.bytes.data(), kHello.data(), kHello.size()) == 0);

    FrameParser parser;
    FrameCollector collector;
    EXPECT_TRUE(parser.feed(kAdxl.data(), 17, collector) == 0);
    EXPECT_TRUE(parser.feed(kAdxl.data() + 17, kAdxl.size() - 17, collector) == 1);
    const Frame& frame = collector.frame;
    EXPECT_TRUE(frame.type == MessageType::StreamAdxl && frame.session == 0x11223344U);

    vhm::SampleBatch batch{};
    EXPECT_TRUE(decode_stream(frame, batch) == DecodeError::None);
    EXPECT_TRUE(batch.count == 1 && batch.samples[0].channel[0] == -2600);
    EXPECT_TRUE(batch.samples[0].channel[1] == 2700 && batch.samples[0].channel[2] == 49700);

    auto broken = kAdxl;
    broken[50] ^= 0x01;
    std::array<std::uint8_t, kAdxl.size() * 2> bad_then_good{};
    std::memcpy(bad_then_good.data(), broken.data(), broken.size());
    std::memcpy(bad_then_good.data() + broken.size(), kAdxl.data(), kAdxl.size());
    FrameParser recovering;
    FrameCollector recovered;
    EXPECT_TRUE(recovering.feed(bad_then_good.data(), bad_then_good.size(), recovered) == 1);
    EXPECT_TRUE(recovering.crc_errors() == 1);

    std::array<std::uint8_t, kAdxl.size() * 2> two_good{};
    std::memcpy(two_good.data(), kAdxl.data(), kAdxl.size());
    std::memcpy(two_good.data() + kAdxl.size(), kAdxl.data(), kAdxl.size());
    FrameParser multi_parser;
    FrameCollector multi;
    EXPECT_TRUE(multi_parser.feed(two_good.data(), two_good.size(), multi) == 2);
    EXPECT_TRUE(multi.count == 2);

    Frame icm{};
    icm.type = MessageType::StreamIcm;
    icm.payload_size = 45;
    put32(icm.payload.data(), 2); put32(icm.payload.data() + 4, 3);
    put64(icm.payload.data() + 8, 4); put64(icm.payload.data() + 16, 5);
    put32(icm.payload.data() + 24, 5000U << 16); put16(icm.payload.data() + 28, 1);
    put16(icm.payload.data() + 30, 0);
    const std::int16_t axes[] = {1, -2, 3, -4, 5, -6};
    for (unsigned i = 0; i < 6; ++i) put16(icm.payload.data() + 32 + i * 2,
                                           static_cast<std::uint16_t>(axes[i]));
    icm.payload[44] = 0xF9;
    EXPECT_TRUE(decode_stream(icm, batch) == DecodeError::None);
    EXPECT_TRUE(batch.samples[0].channel[5] == -6 && batch.samples[0].temperature_raw == -7);

    Frame attitude_frame{};
    attitude_frame.type = MessageType::Attitude;
    attitude_frame.payload_size = 48;
    put32(attitude_frame.payload.data(), 9); put32(attitude_frame.payload.data() + 4, 3);
    put64(attitude_frame.payload.data() + 8, 7); put64(attitude_frame.payload.data() + 16, 1234567);
    put32(attitude_frame.payload.data() + 24, 5000U << 16);
    put16(attitude_frame.payload.data() + 28, 1); put16(attitude_frame.payload.data() + 30, 1);
    put32(attitude_frame.payload.data() + 32, 1U << 30);
    put32(attitude_frame.payload.data() + 36, 0);
    put32(attitude_frame.payload.data() + 40, static_cast<std::uint32_t>(-(1 << 29)));
    put32(attitude_frame.payload.data() + 44, 1U << 28);
    AttitudeSample attitude{};
    EXPECT_TRUE(decode_attitude(attitude_frame, attitude) == DecodeError::None);
    EXPECT_TRUE(attitude.sequence == 7 && attitude.quat_q30[2] == -(1 << 29));

    Frame calibration_frame{};
    calibration_frame.type = MessageType::SaveCalibration;
    calibration_frame.payload_size = 81;
    put32(calibration_frame.payload.data(), 5);
    calibration_frame.payload[4] = 0; calibration_frame.payload[5] = 3;
    calibration_frame.payload[6] = 7;
    put32(calibration_frame.payload.data() + 9, 11);
    put32(calibration_frame.payload.data() + 13, 1234);
    for (unsigned i = 0; i < 16; ++i) put_float(calibration_frame.payload.data() + 17 + i * 4,
                                                static_cast<float>(i) * 0.25F);
    CalibrationRecord calibration{};
    EXPECT_TRUE(decode_calibration_record(calibration_frame, calibration) == DecodeError::None);
    EXPECT_TRUE(calibration.version == 3 && calibration.generation == 11);
    EXPECT_TRUE(std::fabs(calibration.temperature_c - 3.75F) < 0.0001F);

    calibration_frame.payload_size = 80;
    EXPECT_TRUE(decode_calibration_record(calibration_frame, calibration) == DecodeError::WrongLength);

    const Frame hello_request = parse_one(encode_hello_request(0, 1, 0x11223344U));
    EXPECT_TRUE(hello_request.payload_size == 8);
    EXPECT_TRUE(std::memcmp(encode_hello_request(0, 1, 0x11223344U).bytes.data(),
                            kHello.data(), kHello.size()) == 0);
    EXPECT_TRUE(parse_one(encode_set_link_request(9, 2, 7, 5000000)).payload[4] == 0x40);

    const StreamConfig configs[]{{1, 4, 2, 0}, {2, 7, 0x33, 0}};
    const Frame one_config = parse_one(encode_set_config_request(9, 3, 8, 12, configs, 1));
    const Frame two_config = parse_one(encode_set_config_request(9, 4, 9, 13, configs, 2));
    EXPECT_TRUE(one_config.payload_size == 13 && one_config.payload[8] == 1);
    EXPECT_TRUE(two_config.payload_size == 17 && two_config.payload[8] == 2);
    EXPECT_TRUE(parse_one(encode_fetch_request(9, 5, 10, 1600)).payload_size == 8);
    EXPECT_TRUE(parse_one(encode_start_request(9, 6, 11)).payload_size == 4);
    EXPECT_TRUE(parse_one(encode_stop_request(9, 7, 12)).payload_size == 4);
    EXPECT_TRUE(parse_one(encode_get_status_request(9, 8, 13)).payload_size == 4);
    EXPECT_TRUE(parse_one(encode_calibrate_request(9, 9, 14, 1, 2, 5)).payload_size == 8);
    EXPECT_TRUE(parse_one(encode_save_calibration_request(9, 10, 15, 1)).payload_size == 8);

    Frame response{};
    response.type = MessageType::Hello; response.payload_size = 29;
    put32(response.payload.data(), 21); response.payload[4] = 0;
    put16(response.payload.data() + 5, 1); response.payload[7] = 2;
    response.payload[8] = 3; response.payload[9] = 4; put16(response.payload.data() + 10, 55);
    put32(response.payload.data() + 12, 1000); put16(response.payload.data() + 16, 7);
    put16(response.payload.data() + 18, 3); put32(response.payload.data() + 20, 99);
    response.payload[24] = 2;
    HelloResponse hello{};
    EXPECT_TRUE(decode_hello(response, hello) == DecodeError::None);
    EXPECT_TRUE(hello.firmware_build == 55 && hello.state == ProbeState::Measuring);

    response = {}; response.type = MessageType::Stop; response.payload_size = 29;
    put32(response.payload.data(), 22); response.payload[4] = 0;
    response.payload[5] = 1; put64(response.payload.data() + 9, 123);
    response.payload[17] = 2; put64(response.payload.data() + 21, 456);
    StopResponse stopped{};
    EXPECT_TRUE(decode_stop(response, stopped) == DecodeError::None);
    EXPECT_TRUE(stopped.streams[1].last_sequence == 456);

    response = {}; response.type = MessageType::FetchDone; response.payload_size = 27;
    put32(response.payload.data(), 23); response.payload[4] = 0; put32(response.payload.data() + 5, 1400);
    put16(response.payload.data() + 9, 4); put16(response.payload.data() + 11, 2);
    put32(response.payload.data() + 13, 100); put16(response.payload.data() + 17, 3);
    put32(response.payload.data() + 19, 200); response.payload[23] = 2;
    FetchDone done{};
    EXPECT_TRUE(decode_fetch_done(response, done) == DecodeError::None);
    EXPECT_TRUE(done.frames_sent == 4 && done.backlog[1].bytes == 200);

    response = {}; response.type = MessageType::Calibrate; response.payload_size = 16;
    put32(response.payload.data(), 24); response.payload[4] = 7; response.payload[5] = 1;
    response.payload[6] = 2; response.payload[7] = 5; response.payload[8] = 0x20;
    response.payload[9] = 7; response.payload[10] = 1; response.payload[11] = 7;
    put32(response.payload.data() + 12, 8000);
    CalibrationStatus cal_status{};
    EXPECT_TRUE(decode_calibration_status(response, cal_status) == DecodeError::None);
    EXPECT_TRUE(cal_status.sample_count == 8000 && cal_status.window_result == ResultCode::InProgress);

    response = {}; response.type = MessageType::GetStatus; response.payload_size = 115;
    put32(response.payload.data(), 2000); response.payload[4] = 1;
    put16(response.payload.data() + 5, 0x0100); put32(response.payload.data() + 7, 77);
    response.payload[11] = 1; put64(response.payload.data() + 15, 10000); put32(response.payload.data() + 23, 3);
    response.payload[53] = 2; put64(response.payload.data() + 57, 20000); put32(response.payload.data() + 65, 4);
    put32(response.payload.data() + 95, 6); put32(response.payload.data() + 99, 7);
    put32(response.payload.data() + 103, 8); put32(response.payload.data() + 107, 9); response.payload[111] = 1;
    ProbeStatus status{};
    EXPECT_TRUE(decode_status(response, status) == DecodeError::None);
    EXPECT_TRUE(status.streams[1].total_samples == 20000 && status.busy_rejects == 8);

    const struct { MessageType type; std::uint16_t length; } malformed[] = {
        {MessageType::Hello, 28}, {MessageType::SetConfig, 13}, {MessageType::Stop, 28},
        {MessageType::GetStatus, 114}, {MessageType::FetchDone, 26}, {MessageType::Calibrate, 15},
    };
    for (const auto& item : malformed) {
        response = {}; response.type = item.type; response.payload_size = item.length;
        if (item.type == MessageType::Hello) EXPECT_TRUE(decode_hello(response, hello) == DecodeError::WrongLength);
        if (item.type == MessageType::SetConfig) { ConfigResponse value{}; EXPECT_TRUE(decode_set_config(response, value) == DecodeError::WrongLength); }
        if (item.type == MessageType::Stop) EXPECT_TRUE(decode_stop(response, stopped) == DecodeError::WrongLength);
        if (item.type == MessageType::GetStatus) EXPECT_TRUE(decode_status(response, status) == DecodeError::WrongLength);
        if (item.type == MessageType::FetchDone) EXPECT_TRUE(decode_fetch_done(response, done) == DecodeError::WrongLength);
        if (item.type == MessageType::Calibrate) EXPECT_TRUE(decode_calibration_status(response, cal_status) == DecodeError::WrongLength);
    }
}
