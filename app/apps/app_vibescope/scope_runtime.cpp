/**
 * @file scope_runtime.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "scope_runtime.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <new>
#include <hal/hal.h>
#include "scope_recording.h"
#if defined(PLATFORM_BUILD_DESKTOP)
#include <filesystem>
#else
#include <dirent.h>
#include <esp_pthread.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/probe_cdc_esp_idf.hpp>
#include <vhm/probe/probe_client.hpp>
#endif

namespace vibescope {
namespace {
constexpr char kRecordFolder[] = "RECORDS";
constexpr char kScreenFolder[] = "SCREENS";
constexpr char kConfigFolder[] = "CONFIG";
constexpr std::size_t kRecordWriteBufferSize = 16 * 1024;

const char* storage_root()
{
#if defined(PLATFORM_BUILD_DESKTOP)
    if (const auto* temporary = std::getenv("TEMP")) return temporary;
    return ".";
#else
    return "/sd";
#endif
}

std::string storage_directory(const char* folder)
{
    return std::string(storage_root()) + "/" + folder;
}

bool ensure_storage_directory(const std::string& path)
{
#if defined(PLATFORM_BUILD_DESKTOP)
    std::error_code error;
    return std::filesystem::create_directory(path, error) || std::filesystem::is_directory(path, error);
#else
    if (::mkdir(path.c_str(), 0777) == 0) return true;
    struct stat info{};
    return errno == EEXIST && ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

void scan_files(const std::string& directory, const std::string& prefix, std::vector<std::string>& names,
                bool (*filter)(std::string_view))
{
#if defined(PLATFORM_BUILD_DESKTOP)
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
        const auto name = it->path().filename().u8string();
        if (filter(name) && std::filesystem::is_regular_file(it->symlink_status(error)))
            names.push_back(prefix + name);
    }
#else
    if (auto* stream = ::opendir(directory.c_str())) {
        while (const auto* entry = ::readdir(stream))
            if (entry->d_type != DT_DIR && filter(entry->d_name)) names.push_back(prefix + entry->d_name);
        ::closedir(stream);
    }
#endif
}

std::vector<std::string> recording_names()
{
    std::vector<std::string> names;
    scan_files(storage_root(), "", names, is_recording_name);
    scan_files(storage_directory(kRecordFolder), std::string(kRecordFolder) + "/", names, is_recording_name);
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> screenshot_names()
{
    std::vector<std::string> names;
    scan_files(storage_directory(kScreenFolder), "", names, is_screenshot_name);
    std::sort(names.begin(), names.end());
    return names;
}

RecordingFile open_next_file(const std::string& directory, bool screenshot)
{
    if (!ensure_storage_directory(directory)) return {nullptr, directory, EIO};
    const auto* prefix = screenshot ? "SC" : "VN";
    std::vector<std::string> names;
    scan_files(directory, "", names, screenshot ? is_screenshot_name : is_recording_name);
    unsigned last = 0;
    for (const auto& name : names) last = std::max(last, file_sequence(name, prefix));
    return open_numbered_file(directory.c_str(), last + 1, prefix, screenshot ? "BMP" : "VNR");
}

std::uint64_t now_ms()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
void sleep_ms(unsigned ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
void thread_config(const char* name, std::size_t stack, int priority, int core = -1)
{
#if !defined(PLATFORM_BUILD_DESKTOP)
    auto config        = esp_pthread_get_default_config();
    config.thread_name = name;
    config.stack_size  = stack;
    config.prio        = priority;
    config.pin_to_core = core;
    esp_pthread_set_cfg(&config);
#else
    (void)name;
    (void)stack;
    (void)priority;
    (void)core;
#endif
}
}  // namespace

ScopeRuntime::ScopeRuntime()
{
#if defined(PLATFORM_BUILD_DESKTOP)
    _status.demo      = true;
    _status.connected = true;
    _status.message   = "演示数据";
    _want_run         = true;
#endif
}
ScopeRuntime::~ScopeRuntime()
{
    _want_run    = false;
    _want_record = false;
    _alive       = false;
    if (_worker.joinable()) _worker.join();
    if (_recorder.joinable()) _recorder.join();
}
void ScopeRuntime::start()
{
    if (_alive.exchange(true)) return;
    _record_queue.reset(new (std::nothrow) RecordedBatch[kRecordQueueCapacity]);
    thread_config("scope-record", 12288, 3, 1);
    _recorder = std::thread(&ScopeRuntime::recorder, this);
#if !defined(PLATFORM_BUILD_DESKTOP)
    thread_config("scope-probe", 16384, 8);
    _worker = std::thread(&ScopeRuntime::worker, this);
#endif
}
void ScopeRuntime::set_running(bool run)
{
    if (run && _shutdown_requested) return;
    std::lock_guard<std::mutex> lock(_mutex);
    _armed    = false;
    _want_run = run;
    _status.trigger_time_us = 0;
}
void ScopeRuntime::set_fast(bool fast)
{
    _fast = fast;
}

void ScopeRuntime::begin_shutdown()
{
    cancel_calibration();
    set_running(false);
    _want_record = false;
    _shutdown_storage_ready = false;
    _shutdown_requested = true;
}

bool ScopeRuntime::shutdown_ready()
{
    if (!_shutdown_storage_ready) return false;
    std::lock_guard<std::mutex> lock(_mutex);
    return !_status.running && !_calibration_pending;
}
unsigned ScopeRuntime::accel_range_index(Source source) const
{
    return (_requested_ranges.load() >> (source == Source::Adxl ? 0 : 4)) & 0xf;
}
void ScopeRuntime::set_accel_range(Source source, unsigned index)
{
    const bool adxl = source == Source::Adxl;
    if (source == Source::Gyro || index >= (adxl ? kAdxlRangesG.size() : kIcmRangesG.size())) return;
    const unsigned shift = adxl ? 0 : 4;
    _requested_ranges = (_requested_ranges.load() & ~(0xfU << shift)) | (index << shift);
}
void ScopeRuntime::arm(float threshold, unsigned span_ms, unsigned position)
{
    if (_shutdown_requested) return;
    std::lock_guard<std::mutex> lock(_mutex);
    _threshold = std::max(.05F, threshold);
    _trigger_pre_us = span_ms * 10U * position;
    _trigger_post_us = span_ms * 1000U - _trigger_pre_us;
    _status.trigger_time_us = 0;
    _status.trigger_position = position;
    _armed     = true;
    _want_run  = true;
}
void ScopeRuntime::record(bool enabled)
{
    if (enabled && _shutdown_requested) return;
    std::lock_guard<std::mutex> files_lock(_files_mutex);
    if (enabled) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_files_busy) {
            _status.message = "录制失败: 文件操作中";
            return;
        }
        if (!_status.connected) {
            _status.message = "录制失败: 探头未连接";
            return;
        }
        if (_calibration_pending) return;
        if (!_record_queue) {
            _status.message = "录制失败: 缓冲区分配失败";
            return;
        }
    }
    _want_record = enabled;
}

const char* ScopeRuntime::begin_calibration()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_alive || !_status.connected) return "请先连接 USB 探头";
    if (_status.recording || _want_record || _armed) return "请先结束录制或触发";
    if (_calibration_pending || _shutdown_requested || _files_busy) return "设备忙，请稍后重试";
    _calibration_view = {};
    _calibration_view.stage = CalibrationStage::Preparing;
    _calibration_pending = true;
    _calibration_cancel = false;
#if defined(PLATFORM_BUILD_DESKTOP)
    _calibration_demo_start = now_ms();
#endif
    return nullptr;
}

CalibrationView ScopeRuntime::calibration_view()
{
    std::lock_guard<std::mutex> lock(_mutex);
#if defined(PLATFORM_BUILD_DESKTOP)
    // Exercise the complete UI in the desktop simulator using scripted poses.
    auto& view = _calibration_view;
    if (_calibration_pending) {
        constexpr unsigned step_ms = 3500;
        const auto elapsed = now_ms() - _calibration_demo_start;
        view.step.face = static_cast<unsigned>(std::min<std::uint64_t>(elapsed / step_ms, 5));
        const auto step = static_cast<unsigned>(elapsed % step_ms);
        const unsigned axis = view.step.face / 2;
        const float sign = view.step.face % 2 ? -1.F : 1.F;
        view.accel = {Axes{.003F, -.009F, .018F}, Axes{.005F, -.002F, .008F}};
        view.accel[0][axis] = sign * .976F;
        view.accel[1][axis] = sign * 1.002F;
        view.gyro = {.19F, -.65F, .67F};
        view.accel_live = {true, true};
        view.gyro_live = true;
        view.aligned = step >= 1200 || elapsed >= 6 * step_ms;
        view.still = step >= 1750 || elapsed >= 6 * step_ms;
        if (!view.aligned) view.accel = {Axes{.42F, .21F, .88F}, Axes{.43F, .20F, .91F}};
        view.step.sensor = step >= 3000 ? 1 : 0;
        if (elapsed >= 6 * step_ms) {
            view.stage = CalibrationStage::Review;
            view.result_ready = true;
            view.result.adxl_accel_bias_g = {-.0468F, .0471F, .0166F};
            view.result.adxl_accel_scale = {1.0393F, 1.0281F, 1.0740F};
            view.result.icm_accel_bias_g = {-.0003F, -.0011F, .0094F};
            view.result.icm_accel_scale = {1.0007F, 1.0009F, 1.0012F};
        } else if (step >= 2500) {
            view.stage = CalibrationStage::Capture;
            view.remaining_ms = (view.step.sensor ? step_ms : 3000U) - step;
        } else {
            view.stage = CalibrationStage::Place;
            view.remaining_ms = step < 1750 ? 0U : 2500 - step;
        }
    }
#endif
    return _calibration_view;
}

void ScopeRuntime::cancel_calibration()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_calibration_pending || _calibration_view.stage == CalibrationStage::Saving) return;
    _calibration_cancel = true;
#if defined(PLATFORM_BUILD_DESKTOP)
    _calibration_pending = false;
    _calibration_view.stage = CalibrationStage::Cancelled;
#else
    _calibration_view.stage = CalibrationStage::Restoring;
#endif
}

void ScopeRuntime::save_calibration()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_calibration_pending || _calibration_view.stage != CalibrationStage::Review) return;
#if defined(PLATFORM_BUILD_DESKTOP)
    _calibration_pending = false;
    _calibration_view.stage = CalibrationStage::Saved;
#else
    _calibration_view.stage = CalibrationStage::Saving;
#endif
}

bool ScopeRuntime::request_record_files(RecordFilesAction action, std::vector<std::string> names, bool screenshots)
{
    std::lock_guard<std::mutex> lock(_files_mutex);
    if (_files_busy || _want_record || _screenshot_busy || _shutdown_requested) return false;
    _files_action = action;
    _files_screens = screenshots;
    _files_selected = std::move(names);
    if (action == RecordFilesAction::Export) set_running(false);
    _export_requested = action == RecordFilesAction::Export;
    _files_busy = true;
    return true;
}

bool ScopeRuntime::request_screenshot(ScreenshotFrame frame)
{
    std::lock_guard<std::mutex> lock(_files_mutex);
    if (!_alive || _files_busy || _screenshot_busy || _shutdown_requested) return false;
    _screenshot_pending = std::move(frame);
    _screenshot_busy = true;
    return true;
}

std::string ScopeRuntime::take_screenshot_result()
{
    std::lock_guard<std::mutex> lock(_files_mutex);
    std::string result;
    result.swap(_screenshot_result);
    return result;
}

void ScopeRuntime::save_preferences(ScopePreferences preferences)
{
    std::lock_guard<std::mutex> lock(_files_mutex);
    _preferences_to_save = std::move(preferences);
    _preferences_due = now_ms() + 500;
}

std::optional<ScopePreferences> ScopeRuntime::take_loaded_preferences()
{
    std::lock_guard<std::mutex> lock(_files_mutex);
    std::optional<ScopePreferences> result;
    result.swap(_preferences_from_card);
    return result;
}

void ScopeRuntime::service_preferences(bool sd_owned, bool flush)
{
    const auto now = now_ms();
    if (!flush && now < _preferences_poll) return;
    _preferences_poll = now + 100;
    std::optional<ScopePreferences> pending;
    std::uint64_t due;
    {
        std::lock_guard<std::mutex> lock(_files_mutex);
        due = _preferences_due;
        if (flush || now >= due) pending = _preferences_to_save;
    }
    if (_preferences_loaded && !pending) return;
#if !defined(PLATFORM_BUILD_DESKTOP)
    if (!sd_owned && !GetHAL()->isSdCardMounted()) {
        if (GetHAL()->getSdCardState() != hal::ConnectionState::Connecting) _preferences_loaded = true;
        return;
    }
    if (!sd_owned && !GetHAL()->beginSdRecording()) return;
#else
    (void)sd_owned;
#endif
    const auto folder = storage_directory(kConfigFolder);
    const std::array<std::string, 2> paths{folder + "/SET0.CFG", folder + "/SET1.CFG"};
    const auto first = preference_file::read(paths[0]);
    const auto second = preference_file::read(paths[1]);
    const auto active = second.prefs && (!first.prefs || second.generation > first.generation) ? 1U : 0U;
    const auto& latest = active ? second : first;
    if (!_preferences_loaded) {
        std::lock_guard<std::mutex> lock(_files_mutex);
        _preferences_from_card = latest.prefs;
        _preferences_loaded = true;
        std::printf("vibescope-config: %s\n", latest.prefs ? "loaded" : "defaults");
    }
    bool ok = true;
    if (pending) {
        ok = ensure_storage_directory(folder) && preference_file::write(paths[1U - active], *pending,
                                                                       latest.generation + 1);
        std::printf("vibescope-config: %s\n", ok ? "saved" : "save failed");
        std::lock_guard<std::mutex> lock(_files_mutex);
        if (_preferences_due == due) {
            if (ok) _preferences_to_save.reset();
            else _preferences_due = now + 3000;
        }
    }
#if !defined(PLATFORM_BUILD_DESKTOP)
    if (!sd_owned) GetHAL()->endSdRecording(ok);
#endif
}

std::shared_ptr<const RecordFilesResult> ScopeRuntime::record_files_result()
{
    std::lock_guard<std::mutex> lock(_files_mutex);
    return _files_result;
}

void ScopeRuntime::service_record_files()
{
    if (!_files_busy) return;
    RecordFilesAction action;
    bool screens{};
    std::vector<std::string> selected;
    {
        std::lock_guard<std::mutex> lock(_files_mutex);
        action = _files_action;
        screens = _files_screens;
        selected = std::move(_files_selected);
    }
    auto result = std::make_shared<RecordFilesResult>();
    result->action = action;
    result->screenshots = screens;
    if (action == RecordFilesAction::Export && _export_requested) {
        // STOP is delivered by the probe worker. Do not hold the SD lease or
        // reassign USB-C until the measurement loop has left its running state.
        const auto deadline = now_ms() + 3000;
        bool running = false;
        do {
            {
                std::lock_guard<std::mutex> lock(_mutex);
                running = _status.running && !_status.demo;
            }
            if (running && _alive && _export_requested) sleep_ms(20);
        } while (running && _alive && _export_requested && now_ms() < deadline);
        if (running && _export_requested)
            result->message = "探头尚未停止";
        else if (_alive && _export_requested)
            result->message = GetHAL()->exportSdCard(_alive, _export_requested);
    }

#if !defined(PLATFORM_BUILD_DESKTOP)
    const bool owned = GetHAL()->beginSdRecording();
#else
    const bool owned = true;
#endif
    if (!owned) {
        if (result->message.empty()) result->message = "SD 未就绪";
    } else {
        if (action != RecordFilesAction::Preview)
            result->names = screens ? screenshot_names() : recording_names();
        if (action == RecordFilesAction::Delete) {
            // Recordings live in the root and under RECORDS/ (their names carry
            // the prefix); screenshots all live in SCREENS/.
            const auto base = screens ? storage_directory(kScreenFolder) : storage_root();
            unsigned removed = 0;
            for (const auto& name : selected) {
                if (!std::binary_search(result->names.begin(), result->names.end(), name)) continue;
                const auto path = base + "/" + name;
                if (std::remove(path.c_str()) == 0) ++removed;
            }
            result->message = "已删除 " + std::to_string(removed) + " / " + std::to_string(selected.size());
            result->names = screens ? screenshot_names() : recording_names();
        } else if (action == RecordFilesAction::Preview) {
            if (screens && selected.size() == 1 && is_screenshot_name(selected.front())) {
                unsigned width = 0, height = 0;
                const char* error = nullptr;
                const auto path = storage_directory(kScreenFolder) + "/" + selected.front();
                const auto started = now_ms();
                auto* pixels = load_screenshot_bmp(path.c_str(), width, height, error);
                std::printf("vibescope-screen: preview read_ms=%u size=%ux%u ok=%d file=%s\n",
                            static_cast<unsigned>(now_ms() - started), width, height, pixels != nullptr,
                            selected.front().c_str());
                if (pixels) {
                    result->preview_name = selected.front();
                    result->pixels.reset(pixels);
                    result->width = width;
                    result->height = height;
                } else
                    result->message = error ? error : "截屏读取失败";
            } else
                result->message = "截屏读取失败";
        } else if (result->message.empty() && result->names.empty())
            result->message = screens ? "没有截屏文件" : "没有录制文件";
        if (screens && action == RecordFilesAction::Scan && result->message.empty() && !result->names.empty())
            result->message = "点击文件名可预览";
#if !defined(PLATFORM_BUILD_DESKTOP)
        GetHAL()->endSdRecording(true);
#endif
    }
    {
        std::lock_guard<std::mutex> lock(_files_mutex);
        _files_result = std::move(result);
        _export_requested = false;
        _files_busy = false;
    }
}

void ScopeRuntime::clear_locked()
{
    for (auto& history : _histories) history.clear();
    _status.valid             = {};
    _status.last_sample_us    = {};
    _status.trigger_time_us   = 0;
    _status.attitude_valid    = false;
    _status.temperature_valid = false;
    _temperature_age = {};
}

void ScopeRuntime::finish_trigger()
{
    if (!_armed || !_status.trigger_time_us) return;
    const auto end = _status.trigger_time_us + _trigger_post_us;
    if ((_status.last_sample_us[0] < end || _status.last_sample_us[1] < end) &&
        now_ms() < _trigger_deadline_ms) return;
    _armed = false;
    _want_run = false;
    _status.message = "触发已捕获";
}

void ScopeRuntime::accept(const vhm::SampleBatch& batch)
{
    if (!batch.valid_count()) return;
    const bool adxl = batch.stream == vhm::StreamId::Adxl;
    if (!adxl && batch.stream != vhm::StreamId::Icm) return;
    const auto range_g = adxl ? kAdxlRangesG[_active_ranges & 0xf] : kIcmRangesG[_active_ranges >> 4];
    unsigned queue_depth = 0;
    bool record_dropped = false;
    if (_want_record) {
        {
            std::lock_guard<std::mutex> lock(_record_mutex);
            if (_record_count < kRecordQueueCapacity) {
                const auto tail = (_record_head + _record_count) % kRecordQueueCapacity;
                _record_queue[tail] = {batch, range_g};
                ++_record_count;
            } else {
                record_dropped = true;
            }
            queue_depth = static_cast<unsigned>(_record_count);
        }
    }
    std::lock_guard<std::mutex> lock(_mutex);
    _status.record_drops += record_dropped;
    _status.record_queue_peak = std::max(_status.record_queue_peak, queue_depth);
    const auto source = adxl ? 0U : 1U;
    if (_status.trigger_time_us && (!_armed || _status.last_sample_us[source] >= _status.trigger_time_us + _trigger_post_us))
        return;
    ++_status.batches[source];
    const bool near_full_scale = vhm::has_quality(batch.quality, vhm::Quality::Saturated);
    if (near_full_scale) ++_status.near_full_scale_batches[source];
    if (!scope_can_display(batch.quality)) {
        ++_status.rejected_batches[source];
        _status.last_rejected_quality[source] = static_cast<std::uint32_t>(batch.quality);
        _histories[source].clear();
        _status.valid[source] = false;
        if (!adxl) {
            _histories[2].clear();
            _status.valid[2] = false;
        }
        _status.message = "数据质量告警";
        return;
    }
    if (batch.period_q16) {
        _status.rate[source] = 65536000000.F / static_cast<float>(batch.period_q16);
        if (!adxl) _status.rate[2] = _status.rate[1];
    }
    const auto sample_time = [&](std::size_t i) {
        return batch.first_time_us + ((static_cast<std::uint64_t>(i) * batch.period_q16 + 32768) >> 16U);
    };
    const bool armed = _armed;
    std::size_t i = 0;
    for (; i < batch.count; ++i) {
        if (armed && _status.trigger_time_us && i > 0 && sample_time(i - 1) >= _status.trigger_time_us + _trigger_post_us)
            break;
        Axes acceleration{}, gyro{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            acceleration[axis] = convert_accel(batch.samples[i].channel[axis], axis, adxl, _calibration, range_g);
            if (!adxl) gyro[axis] = convert_gyro(batch.samples[i].channel[axis + 3], axis, _calibration);
        }
        _histories[source].push(acceleration, batch.segment, batch.config_id, batch.first_sequence + i, near_full_scale);
        if (!adxl) _histories[2].push(gyro, batch.segment, batch.config_id, batch.first_sequence + i, near_full_scale);
        if (adxl && armed && !_status.trigger_time_us && batch.first_time_us &&
            (static_cast<std::uint64_t>(_histories[0].size() - 1) * batch.period_q16 >> 16U) >= _trigger_pre_us &&
            std::any_of(acceleration.begin(), acceleration.end(), [&](float v) {
                return std::abs(v) >= _threshold;
            })) {
            _status.trigger_time_us = sample_time(i);
            _trigger_deadline_ms = now_ms() + _trigger_post_us / 1000U + 300U;
        }
    }
    if (!i) {
        if (armed) finish_trigger();
        return;
    }
    _status.last_sample_us[source] = sample_time(i - 1);
    if (!adxl) _status.last_sample_us[2] = _status.last_sample_us[1];
    const auto time  = now_ms();
    _last_ms[source] = time;
    ++_status.serial[source];
    _status.valid[source] = _histories[source].size() >= 8;
    if (!adxl) {
        _last_ms[2]      = time;
        _status.valid[2] = _histories[2].size() >= 8;
        ++_status.serial[2];
    }
    if (_status.valid[0] && _status.valid[1] && _status.message == "数据质量告警") _status.message = "采集中";
    if (armed) finish_trigger();
}

void ScopeRuntime::attitude(const vhm::probe::AttitudeSample& sample)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (sample.quality != vhm::Quality::None) {
        _status.attitude_valid = false;
        return;
    }
    double q[4], norm = 0;
    for (int i = 0; i < 4; ++i) {
        q[i] = sample.quat_q30[i] / 1073741824.0;
        norm += q[i] * q[i];
    }
    if (norm < .8 || norm > 1.2) {
        _status.attitude_valid = false;
        return;
    }
    for (auto& v : q) v /= std::sqrt(norm);
    const double w = q[0], x = q[1], y = q[2], z = q[3], degrees = 57.295779513;
    _status.attitude       = {static_cast<float>(std::asin(std::clamp(2 * (w * y - z * x), -1.0, 1.0)) * degrees),
                              static_cast<float>(std::atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)) * degrees),
                              static_cast<float>(std::atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)) * degrees)};
    _status.attitude_valid = true;
    _attitude_ms           = now_ms();
}

ScopeSnapshot ScopeRuntime::snapshot(bool include_samples)
{
#if defined(PLATFORM_BUILD_DESKTOP)
    demo_update();
#endif
    std::lock_guard<std::mutex> lock(_mutex);
#if defined(PLATFORM_BUILD_DESKTOP)
    finish_trigger();
#endif
    auto snapshot             = _status;
    if (!snapshot.connected || (snapshot.running && !_temperature_age.fresh(now_ms())))
        snapshot.temperature_valid = false;
    if (!snapshot.running) snapshot.temperature_live = false;
    snapshot.armed            = _armed;
    snapshot.record_requested = _want_record;
    if (!snapshot.connected || now_ms() - _attitude_ms > 500) snapshot.attitude_valid = false;
    for (std::size_t i = 0; i < 3; ++i) {
        snapshot.generation[i] = _histories[i].generation();
        snapshot.updated_ms[i] = _last_ms[i];
        snapshot.valid[i] =
            snapshot.valid[i] && snapshot.connected && (!snapshot.running || now_ms() - _last_ms[i] < 500);
        if (snapshot.valid[i]) {
            const auto count = static_cast<std::size_t>(snapshot.rate[i] * 2);
            if (include_samples) snapshot.samples[i] = _histories[i].copy(count);
            snapshot.near_full_scale[i] = _histories[i].near_full_scale(count);
        }
    }
    return snapshot;
}

void ScopeRuntime::demo_update()
{
    const auto tick = GetHAL()->millis();
    if (tick - _demo_tick < 40) return;
    const auto elapsed = std::min<std::uint32_t>(tick - _demo_tick, 250);
    _demo_tick         = tick;
    const bool run     = _want_run;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _status.running           = run;
        _status.attitude_valid    = true;
        _attitude_ms              = now_ms();
        _status.attitude          = {8.2F, -3.6F, 24.5F};
        _status.temperature       = 30.4F;
        _status.temperature_valid = true;
        _status.temperature_live = _status.running;
        _temperature_age.observe(now_ms() * 1000, 0, now_ms());
    }
    if (!run) return;
    _active_ranges = _requested_ranges;
    // Same path, units and analysis as the probe; desktop never impersonates a USB device.
    for (int kind = 0; kind < 2; ++kind) {
        const float rate = _fast ? (kind == 0 ? 4000.F : 8000.F) : (kind == 0 ? 250.F : 200.F);
        const float counts_per_g = kind == 0 ? 524288.F / kAdxlRangesG[_active_ranges & 0xf]
                                             : 32768.F / kIcmRangesG[_active_ranges >> 4];
        const int total  = static_cast<int>(rate * elapsed / 1000);
        for (int first = 0; first < total; first += 100) {
            vhm::SampleBatch batch;
            batch.stream         = kind == 0 ? vhm::StreamId::Adxl : vhm::StreamId::Icm;
            batch.count          = static_cast<std::uint16_t>(std::min(100, total - first));
            batch.config_id      = (_active_ranges << 2) | (_fast ? 1 : 2);
            batch.segment        = 1;
            batch.first_sequence = _demo_sequence[kind];
            batch.first_time_us  = static_cast<std::uint64_t>(batch.first_sequence * 1000000.0 / rate);
            batch.period_q16     = static_cast<std::uint32_t>(65536000000.0 / rate);
            for (std::size_t i = 0; i < batch.count; ++i) {
                const double t = (batch.first_sequence + i) / rate;
                for (int axis = 0; axis < 3; ++axis) {
                    const double gain = (axis + 1) / 3.0, f = rate > 500 ? 50 : 20;
                    const double v = gain * (.22 * std::sin(6.283185307 * f * t + axis * .5) +
                                             .06 * std::sin(6.283185307 * f * 2 * t)) +
                                     (axis == 2 ? 1 : 0);
                    batch.samples[i].channel[axis] = static_cast<std::int32_t>(v * counts_per_g);
                    batch.samples[i].channel[axis + 3] =
                        static_cast<std::int32_t>(gain * 30 * std::sin(6.283185307 * 1.5 * t + axis) * 65.536);
                }
                batch.samples[i].temperature_raw = 11;
            }
            accept(batch);
            _demo_sequence[kind] += batch.count;
        }
    }
}

#if !defined(PLATFORM_BUILD_DESKTOP)
class ScopeSink final : public vhm::probe::IProbeEventSink {
public:
    explicit ScopeSink(ScopeRuntime& owner) : _owner(owner)
    {
    }
    void on_batch(const vhm::SampleBatch& batch) override
    {
        const auto start = std::chrono::steady_clock::now();
        _owner.accept(batch);
        const auto elapsed = static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
        std::lock_guard<std::mutex> lock(_owner._mutex);
        _owner._status.ingest_max_us = std::max(_owner._status.ingest_max_us, elapsed);
    }
    void on_attitude(const vhm::probe::AttitudeSample& sample) override
    {
        const auto start = std::chrono::steady_clock::now();
        _owner.attitude(sample);
        const auto elapsed = static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
        std::lock_guard<std::mutex> lock(_owner._mutex);
        _owner._status.attitude_max_us = std::max(_owner._status.attitude_max_us, elapsed);
    }
    void on_temperature(const vhm::probe::TemperatureSample& sample) override
    {
        std::lock_guard<std::mutex> lock(_owner._mutex);
        _owner._status.temperature = 25.F + (1885.F - sample.raw) / 9.05F;
        _owner._status.temperature_valid = sample.valid;
        _owner._status.temperature_live = sample.live;
        _owner._temperature_age.observe(sample.time_us, sample.config_id, now_ms());
    }

private:
    ScopeRuntime& _owner;
};

namespace {
using vhm::probe::LinkPhase;
using vhm::probe::ResultCode;
enum CalibrationCommand : std::uint8_t { QueryCalibration, CaptureFace, CancelCapture, ResetFaces };

std::array<vhm::probe::StreamConfig, 2> scope_streams(bool fast, unsigned ranges)
{
    return {{{1, static_cast<std::uint8_t>(fast ? 4 : 0), static_cast<std::uint8_t>(ranges & 0xf), 0},
             {2, static_cast<std::uint8_t>(fast ? 5 : 0), static_cast<std::uint8_t>(0x10 | (ranges >> 4)), 0}}};
}

bool stop_and_drain(vhm::probe::ProbeClient& client)
{
    if (client.snapshot().phase == LinkPhase::Measuring) {
        vhm::probe::StopResponse stop;
        if (!client.stop(stop)) return false;
    }
    vhm::probe::IProbeEventSink discard;
    const auto deadline = now_ms() + 600;
    while (client.snapshot().phase == LinkPhase::Stopping && now_ms() < deadline)
        if (!client.fetch(16384, discard)) return false;
    return client.snapshot().phase == LinkPhase::Standby;
}

class CalibrationSink final : public vhm::probe::IProbeEventSink {
public:
    void on_batch(const vhm::SampleBatch& batch) override
    {
        const bool adxl = batch.stream == vhm::StreamId::Adxl;
        constexpr auto invalid = vhm::Quality::Saturated | vhm::Quality::FifoOverflow |
                                 vhm::Quality::FormatError | vhm::Quality::QueueOverflow;
        const bool valid = batch.valid_count() && !vhm::has_quality(batch.quality, invalid);
        const auto time = valid ? now_ms() : 0;
        const unsigned sensor = adxl ? 0 : 1;
        _accel_ms[sensor] = time;
        if (!adxl) _gyro_ms = time;
        if (!valid) return;
        const auto& raw = batch.samples[batch.count - 1].channel;
        for (unsigned axis = 0; axis < 3; ++axis) {
            // Fixed Slow calibration ranges: ADXL ±10 g, ICM ±2 g and ±500 dps.
            _accel[sensor][axis] = raw[axis] / (adxl ? 52428.8F : 16384.F);
            if (!adxl) _gyro[axis] = raw[axis + 3] / 65.536F;
        }
    }

    void update(CalibrationView& view, std::uint64_t now) const
    {
        view.accel = _accel;
        view.gyro = _gyro;
        for (unsigned sensor = 0; sensor < _accel_ms.size(); ++sensor)
            view.accel_live[sensor] = _accel_ms[sensor] && now - _accel_ms[sensor] <= CalibrationPlacement::freshness_ms;
        view.gyro_live = _gyro_ms && now - _gyro_ms <= CalibrationPlacement::freshness_ms;
        view.aligned = view.accel_live[0] && view.accel_live[1] &&
                       calibration_aligned(view.step.face, view.accel[0]) &&
                       calibration_aligned(view.step.face, view.accel[1]);
        view.still = view.aligned && view.gyro_live && calibration_gyro_still(view.gyro);
    }

private:
    std::array<Axes, 2> _accel{};
    Axes _gyro{};
    std::array<std::uint64_t, 2> _accel_ms{};
    std::uint64_t _gyro_ms{};
};
}  // namespace

bool ScopeRuntime::run_calibration(vhm::probe::ProbeClient& client, std::uint32_t& config,
                                   bool& own_candidate)
{
    auto view = calibration_view();
    auto cancelled = [&] {
        std::lock_guard<std::mutex> lock(_mutex);
        return _calibration_cancel || !_alive;
    };
    auto publish = [&] {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_calibration_cancel && _calibration_view.stage != CalibrationStage::Saving)
            _calibration_view = view;
    };
    auto finish = [&](CalibrationStage stage, const char* error = nullptr) {
        std::lock_guard<std::mutex> lock(_mutex);
        view.stage = _calibration_cancel && stage != CalibrationStage::Saved && !error
                         ? CalibrationStage::Cancelled : stage;
        view.error = error;
        _calibration_view = view;
        _calibration_pending = false;
    };

    // SAVE_CAL writes the whole candidate record. An unrelated dirty candidate
    // cannot be discarded through this protocol, so never implicitly save one.
    vhm::probe::CalibrationStatus status;
    if (cancelled()) {
        finish(CalibrationStage::Cancelled);
        return true;
    }
    if (!client.calibrate(QueryCalibration, 0, 0, status) || status.result.code != ResultCode::Ok) {
        finish(CalibrationStage::Failed, "无法读取探头标定状态");
        return false;
    }
    if (status.state || (status.candidate_modified && !own_candidate)) {
        finish(CalibrationStage::Failed, "探头有未保存的标定，请断电重连后再试");
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _status.running = false;
    }
    const auto slow = scope_streams(false, 0);
    const bool stopped = stop_and_drain(client);
    auto reset_faces = [&] {
        for (std::uint8_t target : {1, 2})
            if (!client.calibrate(ResetFaces, target, 0, status) || status.result.code != ResultCode::Ok) return false;
        return true;
    };
    const bool reset = stopped && reset_faces();
    const bool started = reset && client.configure(++config, slow.data(), slow.size()) && client.start();
    const char* error = started ? nullptr : "无法启动标定采集";
    bool save = false;
    if (started) {
        CalibrationSink sink;
        CalibrationPlacement placement;
        view.stage = CalibrationStage::Place;
        auto poll_due = now_ms();
        auto samples_seen = poll_due;
        while (!cancelled()) {
            if (!client.fetch(4096, sink)) {
                error = "探头通信中断，请重新连接后再试";
                break;
            }
            const auto now = now_ms();
            sink.update(view, now);
            if (view.accel_live[0] && view.accel_live[1] && view.gyro_live) samples_seen = now;
            if (now - samples_seen > 5000 && view.stage != CalibrationStage::Review) {
                error = "收不到原始读数，请检查探头";
                break;
            }
            const auto face = static_cast<std::uint8_t>(view.step.face);
            const auto target = static_cast<std::uint8_t>(view.step.sensor + 1);
            if (view.stage == CalibrationStage::Place) {
                if (!view.still) placement.reset();
                view.remaining_ms = view.still ? placement.update(face, view.accel, view.gyro, now) : 0;
                if (view.still && !view.remaining_ms && !cancelled()) {
                    if (face == 5) own_candidate = true;
                    if (!client.calibrate(CaptureFace, target, face, status) ||
                        status.result.code != ResultCode::InProgress) {
                        error = "探头无法采集当前面";
                        break;
                    }
                    view.stage = CalibrationStage::Capture;
                    view.retry = false;
                    view.remaining_ms = 1000;
                    poll_due = now + 80;
                }
            } else if (view.stage == CalibrationStage::Capture && now >= poll_due) {
                if (!client.calibrate(QueryCalibration, target, face, status) ||
                    status.result.code != ResultCode::Ok || status.target != target || status.face != face) {
                    error = "无法读取当前面采集进度";
                    break;
                }
                poll_due = now + 80;
                const std::uint32_t target_samples = view.step.sensor == 0 ? 250U : 200U;
                view.remaining_ms = (target_samples - std::min(status.sample_count, target_samples)) *
                                    1000U / target_samples;
                if (!status.state) {
                    const bool accepted = status.window_result == ResultCode::Ok &&
                                          (status.completed_faces & (1U << face));
                    if (accepted && face == 5) {
                        const unsigned valid_flag = view.step.sensor == 0 ? 1U : 4U;
                        if (status.completed_faces != 0x3f || !(status.valid_candidates & valid_flag) ||
                            !status.candidate_modified) {
                            error = "标定结果不完整，请重新开始";
                            break;
                        }
                    }
                    if (!accepted && status.completed_faces == 0x3f) {
                        error = view.step.sensor == 0 ? "ADXL357 拟合未通过，请重新开始"
                                                      : "ICM42688 拟合未通过，请重新开始";
                        break;
                    }
                    if (accepted && view.step.accept()) {
                        if (!client.save_calibration(0, view.result) || view.result.result != ResultCode::Ok ||
                            (view.result.valid_flags & 0x05U) != 0x05U) {
                            error = "无法读取两颗传感器的标定结果";
                            break;
                        }
                        view.result_ready = true;
                        view.stage = CalibrationStage::Review;
                    } else {
                        view.stage = CalibrationStage::Place;
                        view.retry = !accepted;
                        view.aligned = view.still = false;
                        view.remaining_ms = 0;
                        placement.reset();
                    }
                }
            } else if (view.stage == CalibrationStage::Review) {
                save = calibration_view().stage == CalibrationStage::Saving;
                if (save) break;
            }
            publish();
            sleep_ms(5);
        }
    }

    if (reset && !save) client.calibrate(CancelCapture, 0, 0, status);
    const bool drained = stop_and_drain(client);
    if (reset && drained && !save) reset_faces();
    bool saved = false;
    if (save && drained) {
        vhm::probe::CalibrationRecord record;
        const bool received = client.save_calibration(1, record);
        saved = received && record.result == ResultCode::Ok;
        if (saved) {
            own_candidate = false;
            view.result = record;
            std::lock_guard<std::mutex> lock(_mutex);
            _calibration = record;
        } else {
            error = received ? "探头未能保存标定" : "保存结果未确认，请重连探头后检查";
        }
    }
    const unsigned ranges = _requested_ranges;
    const auto previous = scope_streams(_fast, ranges);
    const bool restored = drained && client.configure(++config, previous.data(), previous.size());
    {
        std::lock_guard<std::mutex> lock(_mutex);
        clear_locked();
    }
    if (restored) _active_ranges = ranges;
    if (!restored && !error)
        error = saved ? "标定已保存，采集恢复失败" : "采集恢复失败，正在重连探头";
    finish(saved ? CalibrationStage::Saved : error ? CalibrationStage::Failed : CalibrationStage::Cancelled, error);
    return restored;
}

void ScopeRuntime::worker()
{
    auto& transport = vhm::probe::probe_cdc();
    transport.begin();
    auto client_owner = std::make_unique<vhm::probe::ProbeClient>(transport);
    ScopeSink sink(*this);
    std::uint32_t session = 0x56534200, config = 0;
    std::uint64_t discover = 0, status_due = 0, standby_due = 0;
    bool configured_fast = false;
    bool disconnected    = true;
    bool own_candidate = false;
    while (_alive) {
        if (!transport.connected()) {
            _want_record = false;
            if (!disconnected) {
                client_owner = std::make_unique<vhm::probe::ProbeClient>(transport);
                discover     = 0;
            }
            disconnected = true;
            own_candidate = false;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (_calibration_pending) {
                    _calibration_view.stage = CalibrationStage::Failed;
                    _calibration_view.error = "USB 探头已断开，请重新开始";
                    _calibration_pending = false;
                }
                clear_locked();
                _calibration      = {};
                _status.connected = false;
                _status.running   = false;
                _status.message   = "等待 USB 探头";
            }
            sleep_ms(50);
            continue;
        }
        disconnected = false;
        auto& client = *client_owner;
        auto phase   = client.snapshot().phase;
        if ((phase == vhm::probe::LinkPhase::Disconnected || phase == vhm::probe::LinkPhase::Fault) &&
            now_ms() >= discover) {
            {
                std::lock_guard<std::mutex> lock(_mutex);
                clear_locked();
                _calibration = {};
            }
            if (client.connect(++session)) {
                vhm::probe::CalibrationRecord record;
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    _status.message = "USB 已连接";
                }
                vhm::probe::CalibrationStatus status;
                if (client.calibrate(QueryCalibration, 0, 0, status) &&
                    status.result.code == ResultCode::Ok && !status.candidate_modified &&
                    client.save_calibration(0, record) && record.result == ResultCode::Ok) {
                    std::lock_guard<std::mutex> lock(_mutex);
                    _calibration = record;
                }
            }
            discover = now_ms() + 1000;
        }
        phase = client.snapshot().phase;
        bool calibrating;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            calibrating = _calibration_pending;
        }
        if (calibrating) {
            if (!run_calibration(client, config, own_candidate)) {
                client_owner = std::make_unique<vhm::probe::ProbeClient>(transport);
                discover = now_ms() + 1000;
            }
            continue;
        }
        if (phase == vhm::probe::LinkPhase::Measuring &&
            (!_want_run || configured_fast != _fast.load() || _active_ranges != _requested_ranges.load())) {
            vhm::probe::StopResponse stop;
            client.stop(stop);
        }
        phase = client.snapshot().phase;
        if (phase == vhm::probe::LinkPhase::Standby && _want_run) {
            configured_fast = _fast;
            const unsigned ranges = _requested_ranges;
            const auto streams = scope_streams(configured_fast, ranges);
            {
                std::lock_guard<std::mutex> lock(_mutex);
                clear_locked();
            }
            if (client.configure(++config, streams.data(), streams.size()) && _want_run && client.start()) {
                _active_ranges = ranges;
                std::lock_guard<std::mutex> lock(_mutex);
                _status.message = "采集中";
            }
        }
        phase = client.snapshot().phase;
        if (phase == vhm::probe::LinkPhase::Measuring || phase == vhm::probe::LinkPhase::Stopping) {
            for (int rounds = 0; rounds < 48 && _alive; ++rounds) {
                // Yield promptly to STOP, then fetch through its final sequence boundaries.
                if (phase == vhm::probe::LinkPhase::Measuring &&
                    (!_want_run || _active_ranges != _requested_ranges.load())) break;
                vhm::probe::FetchDone done;
                if (!client.fetch(16384, sink, &done)) break;
                if (client.snapshot().phase == vhm::probe::LinkPhase::Standby) break;
                if (!done.backlog[0].batches && !done.backlog[1].batches) break;
            }
        } else if (phase == vhm::probe::LinkPhase::Standby && now_ms() >= standby_due) {
            // One bounded window at 20 Hz; enough for 10 x 200 Hz ATTITUDE
            // frames plus status, without occupying the fast measurement pump.
            client.fetch(1024, sink);
            standby_due = now_ms() + 50;
        }
        const auto link = client.snapshot();
        if (link.phase != vhm::probe::LinkPhase::Disconnected && link.phase != vhm::probe::LinkPhase::Fault &&
            now_ms() >= status_due) {
            vhm::probe::ProbeStatus status;
            if (client.get_status(status)) {
                std::lock_guard<std::mutex> lock(_mutex);
                _status.queue_drops = status.streams[0].queue_drops + status.streams[1].queue_drops;
            }
            status_due = now_ms() + 5000;
        }
        const auto transport_stats = transport.stats();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            finish_trigger();
            _status.connected =
                link.phase != vhm::probe::LinkPhase::Disconnected && link.phase != vhm::probe::LinkPhase::Fault;
            _status.running       = link.phase == vhm::probe::LinkPhase::Measuring;
            _status.crc_errors    = link.crc_errors;
            _status.gaps          = link.sequence_gaps;
            _status.timeouts      = link.timeouts;
            _status.retries       = link.retries;
            _status.rx_high_water = transport_stats.rx_high_water;
            _status.rx_overruns   = transport_stats.ring_full;
            _status.worker_stack  = uxTaskGetStackHighWaterMark(nullptr);
            if (!_status.connected) clear_locked();
        }
        sleep_ms(link.phase == vhm::probe::LinkPhase::Measuring ? 1 : 10);
    }
    if (client_owner->snapshot().phase == vhm::probe::LinkPhase::Measuring) {
        vhm::probe::StopResponse stop;
        client_owner->stop(stop);
    }
}
#else
void ScopeRuntime::worker()
{
}
#endif

void ScopeRuntime::recorder()
{
    FILE* file            = nullptr;
    std::unique_ptr<char, decltype(&std::free)> write_buffer(nullptr, &std::free);
    RecordingFile screenshot_file;
    ScreenshotFrame screenshot;
    unsigned screenshot_row = 0;
    std::uint64_t screenshot_started = 0;
    bool sd_owned = false, record_failed = false;
    auto release_sd = [&](bool success) {
        if (sd_owned) GetHAL()->endSdRecording(success);
        sd_owned = false;
    };
    std::uint64_t started = 0;
    std::uint64_t write_max_us = 0;
    auto write_u32        = [&](std::uint32_t v) {
        std::uint8_t b[4] = {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8),
                             static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 24)};
        return std::fwrite(b, 1, 4, file) == 4;
    };
    while (_alive || file || screenshot_file.file) {
        if (!file && _files_busy) service_record_files();
        if (_shutdown_requested && !file && !_screenshot_busy) {
            if (!_shutdown_storage_ready) {
                service_preferences(false, true);
                _shutdown_storage_ready = true;
            }
            sleep_ms(5);
            continue;
        }
        if (!_files_busy && !screenshot_file.file) service_preferences(sd_owned);
        if (_want_record && !file && !screenshot_file.file && _alive) {
#if !defined(PLATFORM_BUILD_DESKTOP)
            if (!GetHAL()->beginSdRecording()) {
                _want_record = false;
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    _status.message = "录制失败: SD 未就绪";
                }
                sleep_ms(20);
                continue;
            }
            sd_owned = true;
#endif
            const auto directory = storage_directory(kRecordFolder);
            const auto created = open_next_file(directory, false);
            file = created.file;
            if (!file) {
                _want_record = false;
                release_sd(false);
                std::printf("vibescope-record: create failed path=%s errno=%d\n", created.path.c_str(), created.error);
                std::lock_guard<std::mutex> lock(_mutex);
                _status.message = "录制文件创建失败: " + std::to_string(created.error);
                continue;
            }
            // Configure buffering before the first file operation and keep it alive through fclose.
            write_buffer.reset(static_cast<char*>(std::malloc(kRecordWriteBufferSize)));
            if (!write_buffer ||
                std::setvbuf(file, write_buffer.get(), _IOFBF, kRecordWriteBufferSize) != 0) {
                write_buffer.reset();
                std::printf("vibescope-record: using default write buffer\n");
            }
            const char magic[8] = {'V', 'N', 'R', 'A', 'W', 3, 0, 0};
            bool header_ok      = std::fwrite(magic, 1, 8, file) == 8;
            vhm::probe::CalibrationRecord cal;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                cal = _calibration;
            }
            header_ok        = header_ok && write_u32(cal.valid_flags) && write_u32(cal.generation);
            auto write_float = [&](float value) {
                std::uint32_t bits;
                std::memcpy(&bits, &value, 4);
                return write_u32(bits);
            };
            for (const auto* vector : {&cal.adxl_accel_bias_g, &cal.adxl_accel_scale, &cal.icm_accel_bias_g,
                                       &cal.icm_accel_scale, &cal.icm_gyro_bias_dps})
                for (float value : *vector) header_ok = header_ok && write_float(value);
            header_ok = header_ok && write_float(cal.temperature_c);
            if (!header_ok) {
                std::fclose(file);
                write_buffer.reset();
                file         = nullptr;
                _want_record = false;
                release_sd(false);
                std::lock_guard<std::mutex> lock(_mutex);
                _status.message = "录制文件写入失败";
                continue;
            }
            started = now_ms();
            write_max_us = 0;
            record_failed = false;
            std::printf("vibescope-record: opened=%s\n", created.path.c_str());
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _status.recording      = true;
                _status.recording_path = created.path;
                _status.record_seconds = 0;
                _status.record_drops   = 0;
                _status.record_queue_peak = 0;
            }
        }
        RecordedBatch recorded;
        bool have = false;
        {
            std::lock_guard<std::mutex> lock(_record_mutex);
            if (_record_count) {
                recorded = _record_queue[_record_head];
                _record_head = (_record_head + 1) % kRecordQueueCapacity;
                --_record_count;
                have = true;
            }
        }
        if (file && have) {
            const auto& batch = recorded.samples;
            std::array<std::uint8_t, 44 + 28 * vhm::kMaxSamplesPerBatch> bytes{};
            std::size_t size = 0;
            auto append32    = [&](std::uint32_t value) {
                for (unsigned shift = 0; shift < 32; shift += 8)
                    bytes[size++] = static_cast<std::uint8_t>(value >> shift);
            };
            auto append64 = [&](std::uint64_t value) {
                append32(static_cast<std::uint32_t>(value));
                append32(static_cast<std::uint32_t>(value >> 32));
            };
            append32(static_cast<std::uint32_t>(batch.stream));
            append32(batch.config_id);
            append32(batch.segment);
            append32(static_cast<std::uint32_t>(batch.quality));
            append64(batch.first_sequence);
            append64(batch.first_time_us);
            append32(batch.period_q16);
            append32(batch.count);
            append32(recorded.range_g);
            for (std::size_t i = 0; i < batch.count; ++i) {
                for (auto value : batch.samples[i].channel) append32(static_cast<std::uint32_t>(value));
                append32(static_cast<std::uint32_t>(batch.samples[i].temperature_raw));
            }
            const auto write_started = std::chrono::steady_clock::now();
            const bool ok = std::fwrite(bytes.data(), 1, size, file) == size;
            const auto write_us = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - write_started).count());
            write_max_us = std::max(write_max_us, write_us);
            if (write_us >= 50000) {
                std::size_t depth;
                {
                    std::lock_guard<std::mutex> lock(_record_mutex);
                    depth = _record_count;
                }
                std::printf("vibescope-record: slow_write_us=%llu queue=%u elapsed_ms=%llu\n",
                            static_cast<unsigned long long>(write_us), static_cast<unsigned>(depth),
                            static_cast<unsigned long long>(now_ms() - started));
            }
            if (!ok) {
                record_failed = true;
                _want_record = false;
                std::lock_guard<std::mutex> lock(_mutex);
                _status.message = "录制写入失败";
            }
        }
        if (_screenshot_busy && !screenshot_file.file && _alive) {
            std::lock_guard<std::mutex> lock(_files_mutex);
            screenshot = std::move(_screenshot_pending);
        }
        if (screenshot.pixels) {
            bool ok = true;
            if (!screenshot_file.file) {
                screenshot_started = now_ms();
#if !defined(PLATFORM_BUILD_DESKTOP)
                if (!sd_owned) sd_owned = GetHAL()->beginSdRecording();
                ok = sd_owned;
#endif
                if (ok) {
                    const auto directory = storage_directory(kScreenFolder);
                    screenshot_file = open_next_file(directory, true);
                    ok = screenshot_file.file && write_screenshot_header(screenshot_file.file, screenshot);
                    screenshot_row = 0;
                }
            }
            if (ok) ok = write_screenshot_rows(screenshot_file.file, screenshot, screenshot_row);
            if (!ok || screenshot_row == screenshot.height) {
                if (screenshot_file.file) {
                    ok = std::fclose(screenshot_file.file) == 0 && ok;
                    if (!ok) std::remove(screenshot_file.path.c_str());
                }
                if (!file) release_sd(ok);
                std::printf("vibescope-screen: %s write_ms=%u rotate_ms=%u io_ms=%u dma=%d path=%s\n",
                            ok ? "saved" : "failed", static_cast<unsigned>(now_ms() - screenshot_started),
                            static_cast<unsigned>(screenshot.rotate_us / 1000),
                            static_cast<unsigned>(screenshot.io_us / 1000), screenshot.dma_rows != nullptr,
                            screenshot_file.path.c_str());
                {
                    std::lock_guard<std::mutex> lock(_files_mutex);
                    _screenshot_result = ok ? "截屏已保存" : "截屏保存失败";
                    _screenshot_busy = false;
                }
                screenshot_file = {};
                screenshot = {};
            }
        }
        if (file) {
            std::lock_guard<std::mutex> lock(_mutex);
            _status.record_seconds = static_cast<std::uint32_t>((now_ms() - started) / 1000);
            const auto duration = _record_duration.load();
            if (duration && _status.record_seconds >= duration) _want_record = false;
        }
        if (file && (!_want_record || !_alive) && !have && !screenshot_file.file) {
            const bool ok = std::fclose(file) == 0;
            write_buffer.reset();
            file          = nullptr;
            release_sd(ok && !record_failed);
            std::lock_guard<std::mutex> lock(_mutex);
            std::printf("vibescope-record: closed ok=%u dropped_batches=%u write_max_us=%llu\n",
                        ok && !record_failed, static_cast<unsigned>(_status.record_drops),
                        static_cast<unsigned long long>(write_max_us));
            _status.recording = false;
            if (!ok) _status.message = "录制关闭失败";
        }
        if (!have && !screenshot_file.file) sleep_ms(5);
    }
    service_preferences(false, true);
}
}  // namespace vibescope
