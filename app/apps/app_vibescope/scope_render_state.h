/**
 * @file scope_render_state.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-04
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include "scope_analysis.h"

namespace vibescope {
struct PlotRenderState {
    PlotConfig config;
    std::uint64_t serial{}, generation{}, analysis_serial{}, trigger_time_us{};
    std::size_t first{}, count{}, sample_count{}, spectrum_count{};
    float rate{}, bin_hz{};
    unsigned axis{}, trigger_position{};
    bool valid{}, near_full_scale{}, selected{}, running{}, armed{}, resultant{};

    bool operator==(const PlotRenderState& o) const
    {
        return config.kind == o.config.kind && config.source == o.config.source &&
            config.span_ms == o.config.span_ms && config.auto_scale == o.config.auto_scale &&
            config.spectrum_low_hz == o.config.spectrum_low_hz && config.spectrum_high_hz == o.config.spectrum_high_hz &&
            config.cursor == o.config.cursor && config.all_axes == o.config.all_axes &&
            config.cursor_axis == o.config.cursor_axis &&
            config.spectrum_db_scale == o.config.spectrum_db_scale &&
            config.spectrum_peak_count == o.config.spectrum_peak_count &&
            config.waterfall_palette == o.config.waterfall_palette && config.waterfall_window == o.config.waterfall_window &&
            config.cursor_fraction == o.config.cursor_fraction &&
            config.dual_cursor == o.config.dual_cursor && config.cursor_b_fraction == o.config.cursor_b_fraction &&
            serial == o.serial &&
            generation == o.generation && analysis_serial == o.analysis_serial && first == o.first &&
            count == o.count && sample_count == o.sample_count && spectrum_count == o.spectrum_count &&
            rate == o.rate && bin_hz == o.bin_hz && axis == o.axis && valid == o.valid &&
            near_full_scale == o.near_full_scale && selected == o.selected && running == o.running &&
            trigger_time_us == o.trigger_time_us && trigger_position == o.trigger_position && armed == o.armed &&
            resultant == o.resultant;
    }
};
} // namespace vibescope
