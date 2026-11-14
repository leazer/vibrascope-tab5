/**
 * @file fake_byte_transport.hpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-09-10
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

#include "vhm/probe/byte_transport.hpp"
#include "vhm/probe/frame_codec.hpp"

class FakeByteTransport final : public vhm::probe::IByteTransport {
public:
    using WriteHandler = std::function<void(const std::vector<std::uint8_t>&, FakeByteTransport&)>;

    bool set_baud(std::uint32_t baud) override
    {
        baud_calls.push_back(baud);
        if (fail_baud_change) return false;
        baud_ = baud;
        return true;
    }

    std::size_t write(const std::uint8_t* data, std::size_t size) override
    {
        writes.emplace_back(data, data + size);
        if (on_write) on_write(writes.back(), *this);
        return size;
    }

    std::size_t read(std::uint8_t* data, std::size_t capacity, std::uint32_t timeout_ms) override
    {
        if (rx.empty()) {
            now_ += timeout_ms;
            return 0;
        }
        const std::size_t count = std::min(capacity, std::min(fragment_size, rx.size()));
        std::copy_n(rx.begin(), count, data);
        rx.erase(rx.begin(), rx.begin() + static_cast<std::ptrdiff_t>(count));
        return count;
    }

    std::uint64_t now_ms() const override { return now_; }
    void enqueue(const vhm::probe::FrameBytes& frame)
    {
        rx.insert(rx.end(), frame.bytes.begin(), frame.bytes.begin() + static_cast<std::ptrdiff_t>(frame.size));
    }

    WriteHandler on_write{};
    std::vector<std::vector<std::uint8_t>> writes{};
    std::vector<std::uint32_t> baud_calls{};
    std::vector<std::uint8_t> rx{};
    std::size_t fragment_size{1024};
    bool fail_baud_change{false};
    std::uint32_t baud_{1000000};
    std::uint64_t now_{0};
};
