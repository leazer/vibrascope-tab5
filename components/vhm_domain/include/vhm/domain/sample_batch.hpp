#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vhm {

constexpr std::size_t kMaxSamplesPerBatch = 108;

enum class StreamId : std::uint8_t {
    Adxl = 0x01,
    Icm = 0x02,
    Attitude = 0x20,
};

enum class SampleFormat : std::uint8_t {
    Unknown = 0,
    Adxl20 = 1,
    Icm16 = 2,
    QuaternionQ30 = 3,
};

enum class Quality : std::uint32_t {
    None = 0,
    CrcError = 1U << 0,
    SequenceGap = 1U << 1,
    SegmentChanged = 1U << 2,
    FifoOverflow = 1U << 3,
    Saturated = 1U << 4,
    QueueOverflow = 1U << 5,
    TimeUncertain = 1U << 6,
    AuxiliaryDegraded = 1U << 7,
    FormatError = 1U << 8,
    SegmentStart = 1U << 9,
};

constexpr Quality operator|(Quality left, Quality right)
{
    return static_cast<Quality>(static_cast<std::uint32_t>(left) |
                                static_cast<std::uint32_t>(right));
}

constexpr Quality operator&(Quality left, Quality right)
{
    return static_cast<Quality>(static_cast<std::uint32_t>(left) &
                                static_cast<std::uint32_t>(right));
}

constexpr Quality& operator|=(Quality& left, Quality right)
{
    left = left | right;
    return left;
}

constexpr bool has_quality(Quality value, Quality flag)
{
    return (value & flag) != Quality::None;
}

struct Sample {
    std::array<std::int32_t, 6> channel{};
    std::int32_t temperature_raw{0};
};

struct SampleBatch {
    StreamId stream{StreamId::Adxl};
    SampleFormat format{SampleFormat::Unknown};
    std::uint32_t config_id{0};
    std::uint32_t segment{0};
    std::uint64_t first_sequence{0};
    std::uint64_t first_time_us{0};
    std::uint32_t period_q16{0};
    std::uint16_t accel_range_g{0};
    std::uint16_t gyro_range_dps{0};
    Quality quality{Quality::None};
    std::uint16_t count{0};
    std::array<Sample, kMaxSamplesPerBatch> samples{};

    constexpr bool valid_count() const
    {
        return count > 0 && count <= samples.size();
    }

    constexpr bool usable_for_primary_analysis() const
    {
        constexpr Quality fatal = Quality::CrcError |
                                  Quality::SequenceGap |
                                  Quality::SegmentChanged |
                                  Quality::FifoOverflow |
                                  Quality::Saturated |
                                  Quality::QueueOverflow |
                                  Quality::TimeUncertain |
                                  Quality::FormatError;
        return stream == StreamId::Adxl && valid_count() &&
               !has_quality(quality, fatal);
    }
};

}  // namespace vhm
