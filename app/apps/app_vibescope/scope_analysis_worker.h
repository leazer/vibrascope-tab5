/**
 * @file scope_analysis_worker.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include "scope_analysis.h"
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace vibescope {
struct ScopeAnalysisInput {
    std::array<std::vector<Axes>, 2> samples;
    std::vector<Axes> gyro;
    std::array<bool, 2> resultant_metrics{};
    std::array<bool, 3> resultant_time{};
    std::array<std::uint64_t, 3> last_sample_us{};
    std::uint64_t gyro_serial{}, gyro_generation{};
    std::array<float, 2> rate{};
    std::array<std::size_t, 2> stats_count{};
    std::array<std::size_t, 2> spectrum_bins{}; // FFT zoom target per source; 0 keeps the default window.
    unsigned window_ms{1000};
    std::array<bool, 2> valid{}, near_full_scale{};
    std::array<std::uint64_t, 2> generation{}, serial{}, sample_time_ms{};
};

struct ScopeAnalysisOutput {
    std::array<std::size_t, 2> spectrum_bins{};
    std::array<std::array<Analysis, 3>, 2> axes;
    std::array<Analysis, 2> resultant;
    std::array<std::vector<float>, 3> resultant_samples;
    std::array<bool, 2> resultant_metrics{};
    std::array<bool, 3> resultant_time{};
    std::array<std::uint64_t, 3> last_sample_us{};
    std::uint64_t gyro_serial{}, gyro_generation{};
    std::array<float, 2> rate{};
    unsigned window_ms{1000};
    std::array<bool, 2> valid{}, near_full_scale{};
    std::array<std::uint64_t, 2> generation{}, serial{}, sample_time_ms{};
    std::uint64_t cost_us{};

    bool usable(unsigned source, bool current_valid, std::uint64_t current_generation, std::uint64_t current_serial,
                float current_rate) const;
};

// One executing job plus one replaceable pending frame. Analysis never runs
// while holding the exchange mutex, an acquisition lock, or the LVGL lock.
class ScopeAnalysisWorker {
public:
    using Analyzer = std::function<Analysis(const std::vector<Axes>&, float, std::size_t, std::size_t, std::size_t)>;
    explicit ScopeAnalysisWorker(Analyzer analyzer = analyze);
    ~ScopeAnalysisWorker();
    void start();
    void stop();
    void submit(ScopeAnalysisInput input);
    std::shared_ptr<const ScopeAnalysisOutput> latest();

private:
    void run();
    Analyzer _analyzer;
    std::mutex _mutex;
    std::condition_variable _ready;
    bool _running{};
    std::unique_ptr<ScopeAnalysisInput> _pending;
    std::shared_ptr<const ScopeAnalysisOutput> _output;
    std::thread _thread;
};
}  // namespace vibescope
