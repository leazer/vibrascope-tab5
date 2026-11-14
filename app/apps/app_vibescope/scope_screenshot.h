/**
 * @file scope_screenshot.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-08
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#include <unistd.h>
#endif

namespace vibescope {
struct ScreenshotFrame {
    std::unique_ptr<std::uint16_t, decltype(&std::free)> pixels{nullptr, &std::free};
    std::unique_ptr<std::uint16_t, decltype(&std::free)> dma_rows{nullptr, &std::free};
    unsigned width{}, height{};
    bool native_portrait{};
    std::uint64_t rotate_us{}, io_us{};
};

inline unsigned screenshot_stride(unsigned width) { return (width + 1) & ~1U; }
inline constexpr unsigned kScreenshotBatchRows = 8;

// Screenshots go to PSRAM on the board; plain heap is fine on the desktop.
inline std::uint16_t* allocate_screenshot_pixels(std::size_t bytes)
{
#if defined(ESP_PLATFORM)
    return static_cast<std::uint16_t*>(heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    return static_cast<std::uint16_t*>(std::malloc(bytes));
#endif
}

// Loads the exact flavor of BMP that write_screenshot_header produces: 16 bpp
// BI_BITFIELDS RGB565, top-down, even width (so rows carry no padding bytes).
// Returns a free()-able pixel buffer, or nullptr with `error` set to a static string.
inline std::uint16_t* load_screenshot_bmp(const char* path, unsigned& width, unsigned& height, const char*& error)
{
    width = height = 0;
    error         = nullptr;
    std::uint16_t* pixels = nullptr;
#if defined(_MSC_VER)
    FILE* file = nullptr;
    fopen_s(&file, path, "rb");
#else
    FILE* file = std::fopen(path, "rb");
#endif
    if (!file) {
        error = "无法打开截屏文件";
        return nullptr;
    }
    const auto fail = [&](const char* reason) {
        error = reason;
        std::fclose(file);
        std::free(pixels);
        return static_cast<std::uint16_t*>(nullptr);
    };
    alignas(128) std::uint8_t header[66]{};
    if (std::fread(header, 1, sizeof(header), file) != sizeof(header)) return fail("截屏读取失败");
    const auto le32 = [&](unsigned at) {
        return static_cast<std::uint32_t>(header[at]) | static_cast<std::uint32_t>(header[at + 1]) << 8 |
               static_cast<std::uint32_t>(header[at + 2]) << 16 | static_cast<std::uint32_t>(header[at + 3]) << 24;
    };
    const auto le16 = [&](unsigned at) {
        return static_cast<unsigned>(header[at]) | static_cast<unsigned>(header[at + 1]) << 8;
    };
    const auto data_offset = le32(10);
    const auto raw_height  = static_cast<std::int32_t>(le32(22));
    const auto width_value = le32(18);
    if (header[0] != 'B' || header[1] != 'M' || data_offset < sizeof(header) || raw_height >= 0 ||
        le16(28) != 16 || le32(30) != 3 /* BI_BITFIELDS */ || (width_value & 1) ||
        le32(54) != 0xf800 || le32(58) != 0x07e0 || le32(62) != 0x001f)
        return fail("不支持的图片格式");
    const auto height_value = 0U - static_cast<std::uint32_t>(raw_height);
    const auto image_bytes = static_cast<std::uint64_t>(width_value) * height_value * sizeof(std::uint16_t);
    if (!image_bytes || image_bytes > (8U << 20) || width_value > UINT16_MAX / 2 ||
        height_value > UINT16_MAX || data_offset > LONG_MAX)
        return fail("不支持的图片格式");
    const auto bytes = static_cast<std::size_t>(image_bytes);
#if defined(ESP_PLATFORM)
    // The current writer starts pixels on a sector boundary. Bypass stdio's
    // staging buffer so FatFs can pass aligned PSRAM straight to SDMMC.
    if ((data_offset % 512U) == 0) {
        pixels = static_cast<std::uint16_t*>(heap_caps_aligned_alloc(
            128, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA));
        if (pixels && (lseek(fileno(file), data_offset, SEEK_SET) < 0 ||
                       ::read(fileno(file), pixels, bytes) != static_cast<ssize_t>(bytes)))
            return fail("截屏读取失败");
    }
#endif
    if (!pixels) {
        pixels = allocate_screenshot_pixels(bytes);
        if (!pixels) return fail("内存不足");
        if (std::fseek(file, static_cast<long>(data_offset), SEEK_SET) ||
            std::fread(pixels, 1, bytes, file) != bytes)
            return fail("截屏读取失败");
    }
    width = width_value;
    height = height_value;
    std::fclose(file);
    return pixels;
}

inline bool write_screenshot_bytes(FILE* file, const void* data, std::size_t bytes, bool direct)
{
#if defined(ESP_PLATFORM)
    if (direct) return ::write(fileno(file), data, bytes) == static_cast<ssize_t>(bytes);
#else
    (void)direct;
#endif
    return std::fwrite(data, 1, bytes, file) == bytes;
}

inline bool write_screenshot_header(FILE* file, ScreenshotFrame& frame)
{
    // Top-down BMP with explicit RGB565 channel masks, stored losslessly.
#if defined(ESP_PLATFORM)
    constexpr unsigned caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA;
    frame.dma_rows.reset(static_cast<std::uint16_t*>(heap_caps_aligned_alloc(
        128, screenshot_stride(frame.width) * kScreenshotBatchRows * sizeof(std::uint16_t), caps)));
#endif
    alignas(128) std::array<std::uint8_t, 512> header{'B', 'M'};
    const unsigned offset = frame.dma_rows ? static_cast<unsigned>(header.size()) : 66U;
    const auto set32 = [&](unsigned offset, std::uint32_t value) {
        for (unsigned byte = 0; byte < 4; ++byte)
            header[offset + byte] = static_cast<std::uint8_t>(value >> (8 * byte));
    };
    const auto image_bytes = screenshot_stride(frame.width) * 2 * frame.height;
    set32(2, offset + image_bytes);
    set32(10, offset);
    set32(14, 40);
    set32(18, frame.width);
    set32(22, 0U - frame.height);
    header[26] = 1;
    header[28] = 16;
    set32(30, 3); // BI_BITFIELDS
    set32(34, image_bytes);
    set32(54, 0xf800);
    set32(58, 0x07e0);
    set32(62, 0x001f);
    return write_screenshot_bytes(file, header.data(), offset, frame.dma_rows != nullptr);
}

inline bool write_screenshot_rows(FILE* file, ScreenshotFrame& frame, unsigned& next_row)
{
    using Clock = std::chrono::steady_clock;
    const auto elapsed = [](auto from, auto to) {
        return std::chrono::duration_cast<std::chrono::microseconds>(to - from).count();
    };
    const auto stride = screenshot_stride(frame.width);
    const auto end = std::min(next_row + kScreenshotBatchRows, frame.height);
    const auto* pixels = frame.pixels.get();
    if (frame.dma_rows) {
        auto* rows = frame.dma_rows.get();
        const auto count = end - next_row;
        const auto start = Clock::now();
        for (unsigned x = 0; x < frame.width; ++x) {
            for (unsigned y = 0; y < count; ++y) {
                const auto index = frame.native_portrait ? (frame.width - 1 - x) * frame.height + next_row + y
                                                          : (next_row + y) * frame.width + x;
                rows[y * stride + x] = pixels[index];
            }
        }
        if (stride != frame.width)
            for (unsigned y = 0; y < count; ++y) rows[y * stride + frame.width] = 0;
        const auto encoded = Clock::now();
        const bool ok = write_screenshot_bytes(file, rows, stride * count * sizeof(*rows), true);
        frame.rotate_us += elapsed(start, encoded);
        frame.io_us += elapsed(encoded, Clock::now());
        next_row = end;
        return ok;
    }
    std::array<std::uint16_t, 1280> row{};
    if (stride > row.size()) return false;
    for (; next_row < end; ++next_row) {
        const auto start = Clock::now();
        for (unsigned x = 0; x < frame.width; ++x) {
            const auto index = frame.native_portrait ? (frame.width - 1 - x) * frame.height + next_row
                                                      : next_row * frame.width + x;
            row[x] = pixels[index];
        }
        const auto encoded = Clock::now();
        const bool ok = write_screenshot_bytes(file, row.data(), stride * sizeof(row[0]), false);
        frame.rotate_us += elapsed(start, encoded);
        frame.io_us += elapsed(encoded, Clock::now());
        if (!ok) return false;
    }
    return true;
}
}  // namespace vibescope
