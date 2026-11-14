/**
 * @file app_link_validation.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-09-10
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "app_link_validation.h"

#include <algorithm>
#include <cstdio>
#include <lvgl.h>
#include <mooncake_log.h>

#if !defined(PLATFORM_BUILD_DESKTOP)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/probe_cdc_esp_idf.hpp"
#include "vhm/probe/probe_client.hpp"
#endif

namespace {

constexpr const char* kTag = "linkval";

const char* stage_name(ValidationStage stage)
{
    switch (stage) {
    case ValidationStage::Disconnected: return "DISCONNECTED";
    case ValidationStage::Standby: return "STANDBY";
    case ValidationStage::Measuring: return "MEASURING";
    case ValidationStage::Stopping: return "STOPPING";
    case ValidationStage::Fault: return "FAULT";
    }
    return "UNKNOWN";
}

const char* command_name(ValidationCommand command)
{
    switch (command) {
    case ValidationCommand::None: return "None";
    case ValidationCommand::Connect: return "Connect";
    case ValidationCommand::StartSlow: return "StartSlow";
    case ValidationCommand::StartDefault: return "StartDefault";
    case ValidationCommand::StartHigh: return "StartIcm16k";
    case ValidationCommand::Stop: return "Stop";
    }
    return "Unknown";
}

void command_event(lv_event_t* event)
{
    const auto value = reinterpret_cast<std::uintptr_t>(lv_event_get_user_data(event));
    const auto command = static_cast<ValidationCommand>(value);
    mclog::tagInfo(kTag, "button: {}", command_name(command));
    link_validation_runtime().request(command);
}

lv_obj_t* add_button(lv_obj_t* parent, const char* text, ValidationCommand command,
                     int x, int y, int width)
{
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, 52);
    lv_obj_add_event_cb(button, command_event, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<std::uintptr_t>(command)));
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

}  // namespace

LinkValidationRuntime& link_validation_runtime()
{
    static LinkValidationRuntime runtime;
    return runtime;
}

ValidationSnapshot LinkValidationRuntime::snapshot() const
{
    return {stage_.load(), adxl_samples_.load(), icm_samples_.load(),
            crc_errors_.load(), timeouts_.load(), retries_.load(), bad_sessions_.load(),
            sequence_gaps_.load(), link_errors_.load(), rx_high_water_.load(),
            probe_queue_drops_.load(), probe_fifo_overflows_.load(),
            probe_format_errors_.load()};
}

void LinkValidationRuntime::add_samples(bool adxl, std::uint16_t count)
{
    if (adxl) adxl_samples_.fetch_add(count);
    else icm_samples_.fetch_add(count);
}

#if !defined(PLATFORM_BUILD_DESKTOP)
class CountingSink final : public vhm::probe::IProbeEventSink {
public:
    explicit CountingSink(LinkValidationRuntime& runtime) : runtime_(runtime) {}
    void on_batch(const vhm::SampleBatch& batch) override
    {
        if (batch.stream == vhm::StreamId::Adxl) runtime_.add_samples(true, batch.count);
        else if (batch.stream == vhm::StreamId::Icm) runtime_.add_samples(false, batch.count);
        ++batches_;
        if (batches_ <= 2 || batches_ % 500 == 0)
            mclog::tagInfo(kTag, "batch #{}: stream {} count {} seq {} time_us {} quality {:#04x}",
                           batches_, static_cast<unsigned>(batch.stream), batch.count,
                           batch.first_sequence, batch.first_time_us,
                           static_cast<unsigned>(batch.quality));
    }
    void on_attitude(const vhm::probe::AttitudeSample& sample) override
    {
        ++attitude_;
        if (attitude_ == 1 || attitude_ % 500 == 0)
            mclog::tagInfo(kTag, "attitude #{}: seg {} seq {}", attitude_, sample.segment,
                           sample.sequence);
    }
    void on_time_info(const vhm::probe::Frame&) override { ++time_info_; }
    void on_status(const vhm::probe::Frame&) override { ++status_; }
    std::uint32_t batches() const { return batches_; }
    std::uint32_t attitude() const { return attitude_; }

private:
    LinkValidationRuntime& runtime_;
    std::uint32_t batches_{0};
    std::uint32_t attitude_{0};
    std::uint32_t time_info_{0};
    std::uint32_t status_{0};
};

void link_validation_worker(void* argument)
{
    auto& runtime = *static_cast<LinkValidationRuntime*>(argument);
    auto& transport = vhm::probe::probe_cdc();
    mclog::tagInfo(kTag, "worker start: open probe cdc");
    transport.begin();
    vhm::probe::ProbeClient client(transport);
    CountingSink sink(runtime);
    std::uint32_t session = 0x54414235;
    std::uint32_t config = 1;
    std::uint64_t next_discovery = 0;
    std::uint64_t next_status = 0;
    std::uint64_t next_probe_status = 0;
    std::uint64_t next_fetch_log = 0;
    std::uint32_t fetch_count = 0;
    std::uint32_t frame_count = 0;
    std::uint16_t backlog_max = 0;

    for (;;) {
        const auto command = runtime.command_.exchange(ValidationCommand::None);
        // CDC write() fails whenever the probe is unplugged, which latches Fault; connect() un-sticks both outcomes (and may log one stale-sequence gap after a re-plug).
        const auto phase = client.snapshot().phase;
        if ((command == ValidationCommand::Connect || transport.now_ms() >= next_discovery) &&
            (phase == vhm::probe::LinkPhase::Disconnected ||
             phase == vhm::probe::LinkPhase::Fault)) {
            if (command == ValidationCommand::Connect)
                mclog::tagInfo(kTag, "connect: session {}", session + 1);
            const bool connected = client.connect(++session);
            if (command == ValidationCommand::Connect || connected)
                mclog::tagInfo(kTag, "connect result: {}", connected ? "standby" : "no response");
            next_discovery = transport.now_ms() + 1000;
        } else if (command == ValidationCommand::StartSlow || command == ValidationCommand::StartDefault ||
                   command == ValidationCommand::StartHigh) {
            /* ODR codes follow the frozen probe table: 0 = 250/200 (slow),
             * 1 = 250 Hz ADXL, 4 = 4 kSPS ADXL, 5 = 8 kSPS / 6 = 16 kSPS
             * ICM. The 16k profile runs ADXL at its 250 Hz floor (the
             * frozen protocol always configures both streams) so the wire
             * budget goes to ICM: the TAB5 host completes ~3335 bulk
             * packets/s and ICM 16k alone needs ~3500 with 64-sample
             * frames - borderline, which this mode measures. */
            std::uint8_t adxl_odr = 0, icm_odr = 0;
            if (command == ValidationCommand::StartDefault) { adxl_odr = 4; icm_odr = 5; }
            else if (command == ValidationCommand::StartHigh) { adxl_odr = 1; icm_odr = 6; }
            const vhm::probe::StreamConfig streams[2] = {
                {1, adxl_odr, 0, 0},
                {2, icm_odr, 0x13, 0},
            };
            mclog::tagInfo(kTag, "configure: adxl odr {} icm odr {} (config {})", streams[0].odr_code,
                           streams[1].odr_code, config + 1);
            if (client.configure(++config, streams, 2)) {
                const bool started = client.start();
                mclog::tagInfo(kTag, "configure ok, start result: {}", started);
            } else {
                mclog::tagInfo(kTag, "configure failed (phase {})",
                               stage_name(static_cast<ValidationStage>(client.snapshot().phase)));
            }
        } else if (command == ValidationCommand::Stop) {
            vhm::probe::StopResponse response{};
            if (client.stop(response)) {
                mclog::tagInfo(kTag, "stop ok: boundary adxl {} icm {}",
                               response.streams[0].last_sequence, response.streams[1].last_sequence);
            } else {
                mclog::tagInfo(kTag, "stop failed (phase {})",
                               stage_name(static_cast<ValidationStage>(client.snapshot().phase)));
            }
        }

        const auto link = client.snapshot();
        if (link.phase == vhm::probe::LinkPhase::Measuring || link.phase == vhm::probe::LinkPhase::Stopping) {
            // 探头 8 槽队列在 8k ODR 下约吸收 64ms；drop 按设计触发 end_segment
            // （序号清零），window 在两路空闲时立即关闭，不会等新样本。排水必须
            // 连续：按 backlog 连续 FETCH + 外层只让 1 tick，任何几毫秒以上的
            // 泵停顿都会挤掉 ICM 样本。窗口预算 16384B（>20ms 满线速 12.5KB），
            // 让"预算耗尽"不先于"窗口到期"触发。
            bool fetch_failed = false;
            for (int rounds = 0; rounds < 48; ++rounds) {
                vhm::probe::FetchDone done{};
                if (!client.fetch(16384, sink, &done)) {
                    fetch_failed = true;
                    break;
                }
                ++fetch_count;
                frame_count += done.frames_sent;
                backlog_max = std::max({backlog_max, done.backlog[0].batches,
                                        done.backlog[1].batches});
                if (std::max(done.backlog[0].batches, done.backlog[1].batches) == 0) break;
            }
            if (fetch_failed && transport.now_ms() >= next_fetch_log) {
                mclog::tagInfo(kTag, "fetch failed");
                next_fetch_log = transport.now_ms() + 1000;
            }
        }
        const auto updated = client.snapshot();
        if (updated.phase != vhm::probe::LinkPhase::Disconnected &&
            transport.now_ms() >= next_status) {
            // GET_STATUS 往返加两行长日志会把排水泵停住几十毫秒，而探头 8 槽队列
            // 在 8k ODR 下只够吸收 ~64ms（round-14：probe 明细行每秒一次时 q 恰好
            // 每秒冲到 8KB 满仓）。测量态把 probe 明细行降为 5s 一次，本地计数行
            // 保持 1s。
            const bool pumping = updated.phase == vhm::probe::LinkPhase::Measuring ||
                                 updated.phase == vhm::probe::LinkPhase::Stopping;
            if (!pumping || transport.now_ms() >= next_probe_status) {
                vhm::probe::ProbeStatus status{};
                if (client.get_status(status)) {
                    runtime.probe_queue_drops_.store(status.streams[0].queue_drops +
                                                     status.streams[1].queue_drops);
                    runtime.probe_fifo_overflows_.store(status.streams[0].fifo_overflows +
                                                        status.streams[1].fifo_overflows);
                    runtime.probe_format_errors_.store(status.streams[0].format_time_errors +
                                                       status.streams[1].format_time_errors);
                    mclog::tagInfo(kTag,
                                   "probe: state {} faults {:#x} cfg {} | s1 tot {} seg {} gaps {} "
                                   "q {}B drop {} spi {} ovf {} fmt {} | s2 tot {} seg {} gaps {} "
                                   "q {}B drop {} spi {} ovf {} fmt {}",
                                   static_cast<unsigned>(status.state), status.fault_bits,
                                   status.config_id, status.streams[0].total_samples,
                                   status.streams[0].segment, status.streams[0].sequence_gaps,
                                   status.streams[0].queued_bytes, status.streams[0].queue_drops,
                                   status.streams[0].spi_errors, status.streams[0].fifo_overflows,
                                   status.streams[0].format_time_errors,
                                   status.streams[1].total_samples, status.streams[1].segment,
                                   status.streams[1].sequence_gaps, status.streams[1].queued_bytes,
                                   status.streams[1].queue_drops, status.streams[1].spi_errors,
                                   status.streams[1].fifo_overflows,
                                   status.streams[1].format_time_errors);
                } else {
                    mclog::tagInfo(kTag, "get_status failed (phase {})",
                                   stage_name(static_cast<ValidationStage>(updated.phase)));
                }
                next_probe_status = transport.now_ms() + 5000;
            }
            mclog::tagInfo(kTag,
                           "status {}: adxl {} icm {} att {} | crc {} tmo {} rty {} "
                           "gap {} sess {} | link {} rxhw {} | hwm {} | fetches {} frames {} bmax {}",
                           stage_name(static_cast<ValidationStage>(updated.phase)),
                           runtime.adxl_samples_.load(), runtime.icm_samples_.load(),
                           sink.attitude(), updated.crc_errors, updated.timeouts, updated.retries,
                           updated.sequence_gaps, updated.bad_sessions, runtime.link_errors_.load(),
                           runtime.rx_high_water_.load(), uxTaskGetStackHighWaterMark(nullptr),
                           fetch_count, frame_count, backlog_max);
            fetch_count = 0;
            frame_count = 0;
            backlog_max = 0;
            next_status = transport.now_ms() + 1000;
        }
        runtime.stage_.store(static_cast<ValidationStage>(updated.phase));
        runtime.crc_errors_.store(updated.crc_errors);
        runtime.timeouts_.store(updated.timeouts);
        runtime.retries_.store(updated.retries);
        runtime.bad_sessions_.store(updated.bad_sessions);
        runtime.sequence_gaps_.store(updated.sequence_gaps);
        const auto transport_stats = transport.stats();
        runtime.link_errors_.store(transport_stats.ring_full);
        runtime.rx_high_water_.store(transport_stats.rx_high_water);
        // 测量态只让 1 tick：FETCH 间隔必须短于样本周期（250Hz=4ms），否则探头
        // 队列（约 1 帧/流）必然溢出。空闲态维持 5ms。
        const bool pumping = updated.phase == vhm::probe::LinkPhase::Measuring ||
                             updated.phase == vhm::probe::LinkPhase::Stopping;
        vTaskDelay(pdMS_TO_TICKS(pumping ? 1 : 5));
    }
}

void LinkValidationRuntime::start()
{
    // dispatch_stream 路径上 SampleBatch(~3.1KB)+多层 Frame/FrameBytes(~1KB each) 叠加，
    // 8KB 栈已实测溢出（Stack protection fault in probe_link）。
    const auto created = xTaskCreate(link_validation_worker, "probe_link", 16384, this, 8, nullptr);
    mclog::tagInfo(kTag, "worker task {} (stack 16384 B, prio 8)",
                   created == pdPASS ? "created" : "create FAILED");
}

void LinkValidationRuntime::update_desktop() {}
#else
void LinkValidationRuntime::start() {}
void LinkValidationRuntime::update_desktop()
{
    static std::uint32_t ticks = 0;
    ++ticks;
    const auto command = command_.exchange(ValidationCommand::None);
    if (command == ValidationCommand::Connect) stage_.store(ValidationStage::Standby);
    if (command == ValidationCommand::StartSlow || command == ValidationCommand::StartDefault ||
        command == ValidationCommand::StartHigh)
        stage_.store(ValidationStage::Measuring);
    if (command == ValidationCommand::Stop) stage_.store(ValidationStage::Standby);
    if (stage_.load() == ValidationStage::Measuring && ticks % 3 == 0) {
        adxl_samples_.fetch_add(20);
        icm_samples_.fetch_add(40);
        rx_high_water_.store(640 + ticks % 200);
    }
}
#endif

AppLinkValidation::AppLinkValidation() { setAppInfo().name = "LinkValidation"; }
void AppLinkValidation::onCreate() { open(); }
void AppLinkValidation::onOpen()
{
    link_validation_runtime().start();
    auto* screen = lv_obj_create(nullptr);
    screen_ = screen;
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
    auto* title = lv_label_create(screen);
    lv_label_set_text(title, "VibraNode Link Validation");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(title, 28, 22);
    status_label_ = lv_label_create(screen);
    lv_obj_set_pos(static_cast<lv_obj_t*>(status_label_), 28, 74);
    counters_label_ = lv_label_create(screen);
    lv_obj_set_pos(static_cast<lv_obj_t*>(counters_label_), 28, 122);
    lv_obj_set_style_text_color(static_cast<lv_obj_t*>(status_label_), lv_color_hex(0x5CE1E6), 0);
    lv_obj_set_style_text_color(static_cast<lv_obj_t*>(counters_label_), lv_color_hex(0xD0D8DE), 0);
    add_button(screen, "Connect", ValidationCommand::Connect, 28, 280, 130);
    add_button(screen, "Slow", ValidationCommand::StartSlow, 170, 280, 120);
    add_button(screen, "4k + 8k", ValidationCommand::StartDefault, 302, 280, 150);
    add_button(screen, "16k", ValidationCommand::StartHigh, 464, 280, 150);
    add_button(screen, "STOP", ValidationCommand::Stop, 626, 280, 120);
    lv_screen_load(screen);
}
void AppLinkValidation::onRunning()
{
    link_validation_runtime().update_desktop();
    const auto value = link_validation_runtime().snapshot();
    char status[64]{};
    std::snprintf(status, sizeof(status), "%s", stage_name(value.stage));
    lv_label_set_text(static_cast<lv_obj_t*>(status_label_), status);
    char counters[256]{};
    std::snprintf(counters, sizeof(counters),
                  "ADXL samples: %llu\nICM samples:  %llu\nCRC: %lu  Timeout: %lu  Retry: %lu\nBad session: %lu  Seq gap: %lu\nLink errors: %lu  RX high-water: %lu B\nProbe drop: %lu  FIFO: %lu  Format: %lu",
                  static_cast<unsigned long long>(value.adxl_samples),
                  static_cast<unsigned long long>(value.icm_samples),
                  static_cast<unsigned long>(value.crc_errors), static_cast<unsigned long>(value.timeouts),
                  static_cast<unsigned long>(value.retries), static_cast<unsigned long>(value.bad_sessions),
                  static_cast<unsigned long>(value.sequence_gaps), static_cast<unsigned long>(value.link_errors),
                  static_cast<unsigned long>(value.rx_high_water),
                  static_cast<unsigned long>(value.probe_queue_drops),
                  static_cast<unsigned long>(value.probe_fifo_overflows),
                  static_cast<unsigned long>(value.probe_format_errors));
    lv_label_set_text(static_cast<lv_obj_t*>(counters_label_), counters);
}
void AppLinkValidation::onClose()
{
    if (screen_) lv_obj_delete(static_cast<lv_obj_t*>(screen_));
    screen_ = nullptr; status_label_ = nullptr; counters_label_ = nullptr;
}
