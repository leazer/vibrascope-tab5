/**
 * @file scope_runtime.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include "scope_analysis.h"
#include "scope_calibration.h"
#include "scope_preferences.h"
#include "scope_screenshot.h"
#include "scope_temperature.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace vhm::probe { class ProbeClient; }

namespace vibescope {

struct ScopeSnapshot {
    bool demo{}, connected{}, running{}, armed{}, recording{}, record_requested{};
    std::array<bool, 3> valid{};
    std::array<bool, 3> near_full_scale{};
    std::array<float, 3> rate{4000, 8000, 8000};
    std::array<std::uint64_t, 3> serial{}, generation{}, updated_ms{};
    std::array<std::uint64_t, 3> last_sample_us{};
    std::uint64_t trigger_time_us{};
    unsigned trigger_position{50};
    std::array<std::vector<Axes>, 3> samples;
    Axes attitude{};
    bool attitude_valid{};
    float temperature{};
    bool temperature_valid{}, temperature_live{};
    std::uint32_t crc_errors{}, gaps{}, queue_drops{}, record_drops{}, record_seconds{};
    unsigned record_queue_peak{};
    std::array<std::uint32_t, 2> batches{}, rejected_batches{}, last_rejected_quality{};
    std::array<std::uint32_t, 2> near_full_scale_batches{};
    std::uint32_t ingest_max_us{}, attitude_max_us{}, worker_stack{}, rx_high_water{}, rx_overruns{}, timeouts{},
        retries{};
    std::string message, recording_path;
};

enum class RecordFilesAction { Scan, Delete, Export, Preview };
struct RecordFilesResult {
    RecordFilesAction action{RecordFilesAction::Scan};
    std::vector<std::string> names;
    std::string message;
    // Filled by Preview: one decoded screenshot as RGB565, kept alive by the holder.
    std::unique_ptr<std::uint16_t, void (*)(void*)> pixels{nullptr, &std::free};
    unsigned width{}, height{};
    std::string preview_name;
    bool screenshots{};
};

// Owns the client and pump; no LVGL calls from this class or its workers.
class ScopeRuntime {
public:
    ScopeRuntime();
    ~ScopeRuntime();
    void start();
    void set_running(bool run);
    void set_fast(bool fast);
    unsigned accel_range_index(Source source) const;
    void set_accel_range(Source source, unsigned index);
    void arm(float absolute_threshold_g, unsigned span_ms, unsigned position);
    void record(bool enabled);
    void set_record_duration(unsigned seconds) { _record_duration = seconds; }
    ScopeSnapshot snapshot(bool include_samples = true);
    const char* begin_calibration();
    CalibrationView calibration_view();
    void cancel_calibration();
    void save_calibration();
    bool request_record_files(RecordFilesAction action, std::vector<std::string> names = {}, bool screenshots = false);
    std::shared_ptr<const RecordFilesResult> record_files_result();
    bool record_files_busy() const { return _files_busy; }
    bool record_export_requested() const { return _export_requested; }
    void stop_record_export() { _export_requested = false; }
    bool request_screenshot(ScreenshotFrame frame);
    std::string take_screenshot_result();
    void save_preferences(ScopePreferences preferences);
    std::optional<ScopePreferences> take_loaded_preferences();
    void begin_shutdown();
    bool shutdown_ready();
    void cancel_shutdown() { _shutdown_requested = false; }

private:
    friend class ScopeSink;
    void worker();
    bool run_calibration(vhm::probe::ProbeClient& client, std::uint32_t& config, bool& own_candidate);
    void recorder();
    void service_record_files();
    void service_preferences(bool sd_owned, bool flush = false);
    void demo_update();
    void accept(const vhm::SampleBatch& batch);
    void attitude(const vhm::probe::AttitudeSample& sample);
    void clear_locked();
    void finish_trigger(); // Called with _mutex held.
    std::mutex _mutex;
    std::array<SampleHistory, 3> _histories;
    std::array<std::uint64_t, 3> _last_ms{};
    std::uint64_t _attitude_ms{};
    TemperatureAge _temperature_age;
    vhm::probe::CalibrationRecord _calibration{};
    CalibrationView _calibration_view;
    bool _calibration_pending{}, _calibration_cancel{};
#if defined(PLATFORM_BUILD_DESKTOP)
    std::uint64_t _calibration_demo_start{};
#endif
    ScopeSnapshot _status;
    std::atomic<bool> _alive{false}, _want_run{false}, _fast{true}, _armed{false}, _want_record{false};
    std::atomic<unsigned> _record_duration{0};
    // SET_CONFIG uses ADXL's code in the low nibble and ICM accel's in the high.
    std::atomic<unsigned> _requested_ranges{0x30};
    unsigned _active_ranges{0x30}; // Owned by the producer that also calls accept().
    float _threshold{1.5F};
    std::uint32_t _trigger_pre_us{}, _trigger_post_us{};
    std::uint64_t _trigger_deadline_ms{};
    std::thread _worker, _recorder;
    std::mutex _record_mutex;
    struct RecordedBatch {
        vhm::SampleBatch samples;
        unsigned range_g{};
    };
    // About 1.7 seconds at the measured 300 batches/s. Allocate before capture.
    static constexpr std::size_t kRecordQueueCapacity = 512;
    std::unique_ptr<RecordedBatch[]> _record_queue;
    std::size_t _record_head{}, _record_count{};
    std::mutex _files_mutex;
    std::atomic<bool> _files_busy{false}, _export_requested{false};
    RecordFilesAction _files_action{RecordFilesAction::Scan};
    bool _files_screens{}; // Scan/Delete/Preview the SCREENS folder instead of recordings.
    std::vector<std::string> _files_selected;
    std::shared_ptr<const RecordFilesResult> _files_result;
    std::atomic<bool> _screenshot_busy{false};
    std::atomic<bool> _shutdown_requested{false}, _shutdown_storage_ready{false};
    ScreenshotFrame _screenshot_pending;
    std::string _screenshot_result;
    std::optional<ScopePreferences> _preferences_to_save, _preferences_from_card;
    std::uint64_t _preferences_due{}, _preferences_poll{};
    bool _preferences_loaded{};
    std::uint32_t _demo_tick{};
    std::array<std::uint64_t, 2> _demo_sequence{};
};
}  // namespace vibescope
