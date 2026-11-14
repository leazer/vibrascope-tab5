/**
 * @file scope_analysis_worker.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "scope_analysis_worker.h"
#include <chrono>
#include <utility>
#include <cmath>
#if !defined(PLATFORM_BUILD_DESKTOP)
#include <esp_pthread.h>
#endif

namespace vibescope {
bool ScopeAnalysisOutput::usable(unsigned source, bool current_valid, std::uint64_t current_generation,
                                 std::uint64_t current_serial, float current_rate) const
{
    return source < 2 && current_valid && valid[source] && generation[source] == current_generation &&
           serial[source] <= current_serial && current_rate > 0 &&
           std::abs(rate[source] - current_rate) <= current_rate * .01F;
}

ScopeAnalysisWorker::ScopeAnalysisWorker(Analyzer analyzer) : _analyzer(std::move(analyzer))
{
}
ScopeAnalysisWorker::~ScopeAnalysisWorker()
{
    stop();
}

void ScopeAnalysisWorker::start()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_running) return;
    _running = true;
#if !defined(PLATFORM_BUILD_DESKTOP)
    const auto previous = esp_pthread_get_default_config();
    auto config         = previous;
    config.thread_name  = "scope-analysis";
    config.stack_size   = 16384;
    config.prio         = 2;  // USB pump (8) and LVGL (4) always take precedence.
    config.pin_to_core  = 0;  // Let the higher-priority recorder use CPU1.
    esp_pthread_set_cfg(&config);
#endif
    _thread = std::thread(&ScopeAnalysisWorker::run, this);
#if !defined(PLATFORM_BUILD_DESKTOP)
    esp_pthread_set_cfg(&previous);
#endif
}

void ScopeAnalysisWorker::stop()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _running = false;
        _pending.reset();
    }
    _ready.notify_one();
    if (_thread.joinable()) _thread.join();
}

void ScopeAnalysisWorker::submit(ScopeAnalysisInput input)
{
    auto pending = std::make_unique<ScopeAnalysisInput>(std::move(input));
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_running) return;
        _pending.swap(pending);  // Retire the replaced buffers after unlocking.
    }
    _ready.notify_one();
}

std::shared_ptr<const ScopeAnalysisOutput> ScopeAnalysisWorker::latest()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _output;
}

void ScopeAnalysisWorker::run()
{
    for (;;) {
        std::unique_ptr<ScopeAnalysisInput> input;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _ready.wait(lock, [&] { return !_running || _pending; });
            if (!_running) return;
            input = std::move(_pending);
        }
        const auto started     = std::chrono::steady_clock::now();
        auto output            = std::make_shared<ScopeAnalysisOutput>();
        output->rate           = input->rate;
        output->spectrum_bins  = input->spectrum_bins;
        output->window_ms      = input->window_ms;
        output->valid          = input->valid;
        output->near_full_scale = input->near_full_scale;
        output->generation     = input->generation;
        output->serial         = input->serial;
        output->sample_time_ms = input->sample_time_ms;
        output->resultant_metrics = input->resultant_metrics;
        output->resultant_time = input->resultant_time;
        output->last_sample_us = input->last_sample_us;
        output->gyro_serial = input->gyro_serial;
        output->gyro_generation = input->gyro_generation;
        for (unsigned source = 0; source < 2; ++source)
            if (input->valid[source]) {
                for (unsigned axis = 0; axis < 3; ++axis)
                    output->axes[source][axis] = _analyzer(input->samples[source], input->rate[source], axis,
                        input->stats_count[source], input->spectrum_bins[source]);
                if (input->resultant_metrics[source] || input->resultant_time[source])
                    output->resultant[source] = analyze_resultant(input->samples[source], input->stats_count[source],
                        output->axes[source], input->resultant_time[source] ? &output->resultant_samples[source] : nullptr);
            }
        if (input->resultant_time[2]) resultant_waveform(input->gyro, {}, output->resultant_samples[2]);
        output->cost_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
        std::shared_ptr<const ScopeAnalysisOutput> published = std::move(output);
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _output.swap(published);
        }
        // Pending work can stay non-empty at high ODR. A real timed block lets
        // the idle task service its watchdog; yield() cannot schedule priority 0.
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
}  // namespace vibescope
