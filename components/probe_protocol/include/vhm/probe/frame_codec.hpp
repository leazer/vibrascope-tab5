#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vhm::probe {

constexpr std::size_t kMaxFrameSize = 1024;
constexpr std::size_t kMaxPayloadSize = 1005;

enum class MessageType : std::uint8_t {
    Hello = 0x01,
    SetLink = 0x02,
    SetConfig = 0x03,
    Start = 0x04,
    Fetch = 0x05,
    Stop = 0x06,
    Calibrate = 0x07,
    SaveCalibration = 0x08,
    GetStatus = 0x09,
    StreamAdxl = 0x10,
    StreamIcm = 0x11,
    TimeInfo = 0x12,
    Status = 0x13,
    FetchDone = 0x14,
    Attitude = 0x20,
    Temperature = 0x21,
};

struct Frame {
    MessageType type{MessageType::Hello};
    std::uint32_t session{0};
    std::uint32_t sequence{0};
    std::uint16_t payload_size{0};
    std::array<std::uint8_t, kMaxPayloadSize> payload{};
};

struct FrameBytes {
    std::array<std::uint8_t, kMaxFrameSize> bytes{};
    std::size_t size{0};
};

class IFrameSink {
public:
    virtual ~IFrameSink() = default;
    virtual void on_frame(const Frame& frame) = 0;
};

std::uint32_t crc32c(const std::uint8_t* data, std::size_t size);

bool build_frame(MessageType type, std::uint32_t session, std::uint32_t sequence,
                 const std::uint8_t* payload, std::size_t payload_size,
                 FrameBytes& output);

class FrameParser {
public:
    std::size_t feed(const std::uint8_t* data, std::size_t size, IFrameSink& sink);

    std::uint32_t crc_errors() const { return crc_errors_; }
    std::uint32_t overflow_errors() const { return overflow_errors_; }
    void reset();

private:
    void discard_prefix(std::size_t count);
    std::size_t parse_available(IFrameSink& sink);

    std::array<std::uint8_t, kMaxFrameSize> buffer_{};
    std::size_t used_{0};
    std::uint32_t crc_errors_{0};
    std::uint32_t overflow_errors_{0};
};

}  // namespace vhm::probe
