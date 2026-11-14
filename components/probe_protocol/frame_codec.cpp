#include "vhm/probe/frame_codec.hpp"

#include <cstring>

namespace vhm::probe {
namespace {

constexpr std::array<std::uint8_t, 4> kHeader{{0xA5, 0x5A, 0xC3, 0x3C}};
constexpr std::uint32_t kCrcPolynomial = 0x82F63B78U;

std::uint16_t read16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(data[1] << 8);
}

std::uint32_t read32(const std::uint8_t* data)
{
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

void write16(std::uint8_t* output, std::uint16_t value)
{
    output[0] = static_cast<std::uint8_t>(value);
    output[1] = static_cast<std::uint8_t>(value >> 8);
}

void write32(std::uint8_t* output, std::uint32_t value)
{
    write16(output, static_cast<std::uint16_t>(value));
    write16(output + 2, static_cast<std::uint16_t>(value >> 16));
}

}  // namespace

std::uint32_t crc32c(const std::uint8_t* data, std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? kCrcPolynomial : 0U);
    }
    return crc ^ 0xFFFFFFFFU;
}

bool build_frame(MessageType type, std::uint32_t session, std::uint32_t sequence,
                 const std::uint8_t* payload, std::size_t payload_size,
                 FrameBytes& output)
{
    if (payload_size > kMaxPayloadSize || (payload_size != 0 && payload == nullptr))
        return false;
    output.bytes[0] = kHeader[0]; output.bytes[1] = kHeader[1];
    output.bytes[2] = kHeader[2]; output.bytes[3] = kHeader[3];
    output.bytes[4] = static_cast<std::uint8_t>(type);
    write16(output.bytes.data() + 5, static_cast<std::uint16_t>(payload_size));
    write32(output.bytes.data() + 7, session);
    write32(output.bytes.data() + 11, sequence);
    if (payload_size != 0)
        std::memcpy(output.bytes.data() + 15, payload, payload_size);
    write32(output.bytes.data() + 15 + payload_size,
            crc32c(output.bytes.data() + 4, 11 + payload_size));
    output.size = 19 + payload_size;
    return true;
}

void FrameParser::discard_prefix(std::size_t count)
{
    if (count >= used_) {
        used_ = 0;
        return;
    }
    std::memmove(buffer_.data(), buffer_.data() + count, used_ - count);
    used_ -= count;
}

std::size_t FrameParser::parse_available(IFrameSink& sink)
{
    std::size_t produced = 0;
    while (true) {
        std::size_t start = 0;
        while (start + kHeader.size() <= used_ &&
               std::memcmp(buffer_.data() + start, kHeader.data(), kHeader.size()) != 0)
            ++start;
        if (start + kHeader.size() > used_) {
            if (used_ > 3) discard_prefix(used_ - 3);
            break;
        }
        if (start != 0) discard_prefix(start);
        if (used_ < 19) break;

        const std::size_t payload_size = read16(buffer_.data() + 5);
        if (payload_size > kMaxPayloadSize) {
            discard_prefix(1);
            continue;
        }
        const std::size_t total = 19 + payload_size;
        if (used_ < total) break;
        const std::uint32_t expected = read32(buffer_.data() + total - 4);
        if (crc32c(buffer_.data() + 4, 11 + payload_size) != expected) {
            ++crc_errors_;
            discard_prefix(1);
            continue;
        }

        Frame frame{};
        frame.type = static_cast<MessageType>(buffer_[4]);
        frame.session = read32(buffer_.data() + 7);
        frame.sequence = read32(buffer_.data() + 11);
        frame.payload_size = static_cast<std::uint16_t>(payload_size);
        if (payload_size != 0)
            std::memcpy(frame.payload.data(), buffer_.data() + 15, payload_size);
        discard_prefix(total);
        sink.on_frame(frame);
        ++produced;
    }
    return produced;
}

std::size_t FrameParser::feed(const std::uint8_t* data, std::size_t size, IFrameSink& sink)
{
    if (size != 0 && data == nullptr) return 0;
    std::size_t produced = parse_available(sink);
    for (std::size_t i = 0; i < size; ++i) {
        if (used_ == buffer_.size()) {
            ++overflow_errors_;
            discard_prefix(1);
        }
        buffer_[used_++] = data[i];
        produced += parse_available(sink);
    }
    return produced;
}

void FrameParser::reset()
{
    used_ = 0;
}

}  // namespace vhm::probe
