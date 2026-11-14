/**
 * @file byte_transport.hpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-09-10
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace vhm::probe {

class IByteTransport {
public:
    virtual ~IByteTransport() = default;
    virtual bool set_baud(std::uint32_t baud) = 0;
    virtual std::size_t write(const std::uint8_t* data, std::size_t size) = 0;
    virtual std::size_t read(std::uint8_t* data, std::size_t capacity,
                             std::uint32_t timeout_ms) = 0;
    virtual std::uint64_t now_ms() const = 0;
};

}  // namespace vhm::probe
