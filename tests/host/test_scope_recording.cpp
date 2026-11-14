/**
 * @file test_scope_recording.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "test_support.hpp"
#include "apps/app_vibescope/scope_recording.h"
#include "apps/app_vibescope/scope_screenshot.h"
#include <fstream>
#include <chrono>
#include <filesystem>
#include <cstdio>

void run_scope_recording_tests()
{
    using namespace vibescope;
    const auto directory = std::filesystem::temp_directory_path() /
        ("vnr-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    EXPECT_TRUE(std::filesystem::create_directory(directory));
    auto first = open_numbered_file(directory.string().c_str(), 1, "VN", "VNR");
    EXPECT_TRUE(first.file != nullptr);
    if (!first.file) return;
    EXPECT_TRUE(std::filesystem::path(first.path).filename().string() == "VN000001.VNR");
    std::fputs("keep", first.file);
    std::fclose(first.file);
    auto second = open_numbered_file(directory.string().c_str(), 1, "VN", "VNR");
    EXPECT_TRUE(second.file != nullptr && std::filesystem::path(second.path).filename().string() == "VN000002.VNR");
    if (second.file) std::fclose(second.file);
    char value[5]{};
    FILE* check = nullptr;
#if defined(_WIN32)
    fopen_s(&check, first.path.c_str(), "rb");
#else
    check = std::fopen(first.path.c_str(), "rb");
#endif
    EXPECT_TRUE(check != nullptr);
    if (check) { EXPECT_TRUE(std::fread(value, 1, 4, check) == 4); std::fclose(check); }
    EXPECT_TRUE(std::string(value) == "keep");
    auto missing = open_numbered_file((directory / "missing").string().c_str(), 1, "VN", "VNR");
    EXPECT_TRUE(!missing.file && missing.error != 0);
    EXPECT_TRUE(file_sequence("VN000123.VNR", "VN") == 123);
    EXPECT_TRUE(file_sequence("VN0A0123.VNR", "VN") == 0);
    EXPECT_TRUE(file_sequence("SC000123.BMP", "VN") == 0);
    auto bitmap = open_numbered_file(directory.string().c_str(), 1, "SC", "BMP");
    EXPECT_TRUE(std::filesystem::path(bitmap.path).filename().string() == "SC000001.BMP");
    EXPECT_TRUE(bitmap.file != nullptr);
    if (bitmap.file) {
        ScreenshotFrame frame;
        frame.width = 2;
        frame.height = 2;
        frame.pixels.reset(allocate_screenshot_pixels(8));
        const std::array<std::uint16_t, 4> expected{0xf800, 0x07e0, 0x001f, 0xffff};
        if (frame.pixels) std::copy(expected.begin(), expected.end(), frame.pixels.get());
        unsigned row = 0;
        EXPECT_TRUE(frame.pixels && write_screenshot_header(bitmap.file, frame) &&
                    write_screenshot_rows(bitmap.file, frame, row));
        std::fclose(bitmap.file);
        const auto aligned = (directory / "ALIGNED.BMP").string();
        {
            std::ifstream input(bitmap.path, std::ios::binary);
            std::array<char, 512> header{};
            std::array<char, 8> payload{};
            input.read(header.data(), 66).read(payload.data(), payload.size());
            EXPECT_TRUE(input.good());
            header[2] = 8;
            header[3] = 2;
            header[10] = 0;
            header[11] = 2;
            std::ofstream output(aligned, std::ios::binary);
            output.write(header.data(), header.size()).write(payload.data(), payload.size());
        }
        for (const auto& path : {bitmap.path, aligned}) {
            unsigned width = 0, height = 0;
            const char* error = nullptr;
            std::unique_ptr<std::uint16_t, decltype(&std::free)> pixels(
                load_screenshot_bmp(path.c_str(), width, height, error), &std::free);
            EXPECT_TRUE(pixels && !error && width == 2 && height == 2 &&
                        std::equal(expected.begin(), expected.end(), pixels.get()));
            std::filesystem::remove(path);
        }
    }
    std::filesystem::remove(first.path);
    std::filesystem::remove(second.path);
    std::filesystem::remove(directory);
}
