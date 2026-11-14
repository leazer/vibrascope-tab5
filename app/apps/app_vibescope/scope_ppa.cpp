/**
 * @file scope_ppa.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "scope_ppa.h"

#if defined(ESP_PLATFORM)
#include "sdkconfig.h"
#endif

#if defined(ESP_PLATFORM) && CONFIG_IDF_TARGET_ESP32P4
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "lvgl.h"
#include "src/draw/lv_draw_private.h"
#include "src/draw/sw/lv_draw_sw.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_private/esp_cache_private.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
extern "C" {
#include "esp_async_fbcpy.h"
}

namespace vibescope {
namespace {
constexpr std::uint8_t kUnitId     = 81;
constexpr std::uint32_t kMinPixels = 4096;
PpaStats stats;
struct Unit {
    lv_draw_unit_t base;
    ppa_client_handle_t srm, fill;
    esp_async_fbcpy_handle_t copy;
    SemaphoreHandle_t copy_done;
};
Unit* unit;
bool copy_complete(esp_async_fbcpy_handle_t, esp_async_fbcpy_event_data_t*, void* context)
{
    auto* u = static_cast<Unit*>(context);
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(u->copy_done, &woken);
    return woken == pdTRUE;
}
int32_t (*original_sw_dispatch)(lv_draw_unit_t*, lv_layer_t*);

int32_t profile_sw_dispatch(lv_draw_unit_t* draw_unit, lv_layer_t* layer)
{
    // Local LVGL 9.2 software unit ID is 1. Capture metadata before dispatch,
    // which completes synchronously and can release the task.
    const auto* task = lv_draw_get_next_available_task(layer, nullptr, 1);
    const auto type = task ? static_cast<unsigned>(task->type) : 0U;
    const auto started = esp_timer_get_time();
    const auto result = original_sw_dispatch(draw_unit, layer);
    if (result == 1 && type < 14) {
        stats.sw_us[type] += static_cast<std::uint64_t>(esp_timer_get_time() - started);
        ++stats.sw_tasks[type];
    }
    return result;
}

bool intersect(lv_area_t& result, const lv_area_t& a, const lv_area_t& b)
{
    result.x1 = std::max(a.x1, b.x1);
    result.y1 = std::max(a.y1, b.y1);
    result.x2 = std::min(a.x2, b.x2);
    result.y2 = std::min(a.y2, b.y2);
    return result.x1 <= result.x2 && result.y1 <= result.y2;
}

bool eligible(lv_draw_task_t* t)
{
    const auto* base = static_cast<const lv_draw_dsc_base_t*>(t->draw_dsc);
    if (!base || !base->layer || base->layer->color_format != LV_COLOR_FORMAT_RGB565) return false;
    lv_area_t a;
    if (!intersect(a, t->area, t->clip_area) || std::uint64_t(a.x2 - a.x1 + 1) * (a.y2 - a.y1 + 1) < kMinPixels)
        return false;
    if (t->type == LV_DRAW_TASK_TYPE_FILL) {
        const auto* d = static_cast<const lv_draw_fill_dsc_t*>(t->draw_dsc);
        return d->opa == LV_OPA_COVER && d->radius == 0 && d->grad.dir == LV_GRAD_DIR_NONE;
    }
    if (t->type != LV_DRAW_TASK_TYPE_IMAGE) return false;
    const auto* d = static_cast<const lv_draw_image_dsc_t*>(t->draw_dsc);
    return d->src && d->header.cf == LV_COLOR_FORMAT_RGB565 && d->opa == LV_OPA_COVER &&
           d->blend_mode == LV_BLEND_MODE_NORMAL && d->recolor_opa == LV_OPA_TRANSP && !d->rotation && !d->skew_x &&
           !d->skew_y && d->scale_x == LV_SCALE_NONE && d->scale_y == LV_SCALE_NONE && !d->bitmap_mask_src &&
           !d->clip_radius && !d->tile && !d->sup && lv_image_src_get_type(d->src) == LV_IMAGE_SRC_VARIABLE;
}

int32_t evaluate(lv_draw_unit_t*, lv_draw_task_t* t)
{
    if (!eligible(t)) {
        ++stats.rejected;
        return 0;
    }
    if (t->preference_score > 70) {
        t->preference_score       = 70;
        t->preferred_draw_unit_id = kUnitId;
    }
    return 1;
}

// Validate a real allocation, not just an aligned-up declaration of its size.
bool destination(lv_draw_buf_t* b)
{
    return b && b->data && b->header.cf == LV_COLOR_FORMAT_RGB565 &&
           (esp_ptr_internal(b->data) || esp_ptr_external_ram(b->data)) && stats.alignment &&
           !(reinterpret_cast<std::uintptr_t>(b->data) % stats.alignment) && b->data_size &&
           !(b->data_size % stats.alignment) && b->header.h && b->header.stride >= b->header.w * 2u &&
           !(b->header.stride % 2u) && std::uint64_t(b->header.stride) * b->header.h <= b->data_size;
}

bool draw(Unit* u, lv_layer_t* layer, lv_draw_task_t* t, std::uint64_t& pixels)
{
    auto* b = layer->draw_buf;
    if (!destination(b)) return false;
    lv_area_t a, clipped;
    if (!intersect(clipped, t->area, t->clip_area) || !intersect(a, clipped, layer->buf_area)) return false;
    const int dx = a.x1 - layer->buf_area.x1, dy = a.y1 - layer->buf_area.y1;
    const int w = a.x2 - a.x1 + 1, h = a.y2 - a.y1 + 1;
    pixels = std::uint64_t(w) * h;
    if (pixels < kMinPixels || dx < 0 || dy < 0 || std::uint64_t(dx) + w > b->header.w ||
        std::uint64_t(dy) + h > b->header.h)
        return false;

    // The driver invalidates complete rows, rounded to the external cache line.
    // Keep that entire span inside this allocation, including its tail padding.
    const auto alignment     = std::uintptr_t(stats.alignment);
    const auto start         = std::uintptr_t(dy) * b->header.stride;
    const auto end           = std::uintptr_t(dy + h) * b->header.stride;
    const auto aligned_start = start / alignment * alignment;
    const auto aligned_end   = (end + alignment - 1) / alignment * alignment;
    if (aligned_end > b->data_size) return false;

    esp_err_t result;
    if (t->type == LV_DRAW_TASK_TYPE_FILL) {
        const auto* d = static_cast<const lv_draw_fill_dsc_t*>(t->draw_dsc);
        ppa_fill_oper_config_t c{};
        c.out.buffer          = b->data;
        c.out.buffer_size     = b->data_size;
        c.out.pic_w           = b->header.stride / 2;
        c.out.pic_h           = b->header.h;
        c.out.block_offset_x  = dx;
        c.out.block_offset_y  = dy;
        c.out.fill_cm         = PPA_FILL_COLOR_MODE_RGB565;
        c.fill_block_w        = w;
        c.fill_block_h        = h;
        c.fill_argb_color.val = lv_color_to_u32(d->color);
        c.mode                = PPA_TRANS_MODE_BLOCKING;
        result                = ppa_do_fill(u->fill, &c);
    } else {
        const auto* d     = static_cast<const lv_draw_image_dsc_t*>(t->draw_dsc);
        const auto* image = static_cast<const lv_image_dsc_t*>(d->src);
        const auto& ih    = image->header;
        // Match 9.2's non-tiled lv_draw_image_normal_helper: coords (task
        // area), not image_area (which only the tiled helper consumes).
        const int sx = a.x1 - t->area.x1, sy = a.y1 - t->area.y1;
        if (ih.magic != LV_IMAGE_HEADER_MAGIC || ih.cf != LV_COLOR_FORMAT_RGB565 ||
            (ih.flags & LV_IMAGE_FLAGS_COMPRESSED) || !image->data || !ih.h || ih.stride < ih.w * 2u ||
            ih.stride % 2u || std::uint64_t(ih.stride) * ih.h > image->data_size || sx < 0 || sy < 0 ||
            std::uint64_t(sx) + w > ih.w || std::uint64_t(sy) + h > ih.h)
            return false;
        // SRM is not in-place; reject even partially overlapping allocations.
        const auto src = reinterpret_cast<std::uintptr_t>(image->data);
        const auto dst = reinterpret_cast<std::uintptr_t>(b->data);
        if (src <= dst ? dst - src < image->data_size : src - dst < b->data_size) return false;
        // IDF 5.4.4 SRM invalidates output before DMA. Preserve pixels previously
        // drawn by software outside this rectangle but inside the same rows.
        const auto cache_started = esp_timer_get_time();
        result = esp_cache_msync(b->data + aligned_start, aligned_end - aligned_start, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        stats.cache_us += static_cast<std::uint64_t>(esp_timer_get_time() - cache_started);
        if (result != ESP_OK) {
            ++stats.errors;
            return false;
        }
        if (u->copy) {
            // Preserve software-drawn neighbors before invalidating complete
            // destination rows. The DMA copier performs no cache maintenance.
            result = esp_cache_msync(b->data + aligned_start, aligned_end - aligned_start,
                                     ESP_CACHE_MSYNC_FLAG_DIR_M2C);
            if (result != ESP_OK) { ++stats.errors; return false; }
            result = esp_cache_msync(const_cast<uint8_t*>(image->data) + sy * ih.stride,
                                     h * ih.stride, ESP_CACHE_MSYNC_FLAG_DIR_C2M |
                                     ESP_CACHE_MSYNC_FLAG_UNALIGNED);
            if (result != ESP_OK) { ++stats.errors; return false; }
            esp_async_fbcpy_trans_desc_t copy{};
            copy.src_buffer = image->data;
            copy.dst_buffer = b->data;
            copy.src_buffer_size_x = ih.stride / 2;
            copy.src_buffer_size_y = ih.h;
            copy.dst_buffer_size_x = b->header.stride / 2;
            copy.dst_buffer_size_y = b->header.h;
            copy.src_offset_x = sx;
            copy.src_offset_y = sy;
            copy.dst_offset_x = dx;
            copy.dst_offset_y = dy;
            copy.copy_size_x = w;
            copy.copy_size_y = h;
            copy.pixel_format_unique_id.color_type_id = COLOR_TYPE_ID(COLOR_SPACE_RGB, COLOR_PIXEL_RGB565);
            result = esp_async_fbcpy(u->copy, &copy, copy_complete, u);
            if (result != ESP_OK) { ++stats.errors; return false; }
            // Do not release the LVGL source or destination until DMA is done.
            xSemaphoreTake(u->copy_done, portMAX_DELAY);
            ++stats.dma_image_tasks;
            return true;
        }
        ppa_srm_oper_config_t c{};
        c.in.buffer          = image->data;
        c.in.pic_w           = ih.stride / 2;
        c.in.pic_h           = ih.h;
        c.in.block_w         = w;
        c.in.block_h         = h;
        c.in.block_offset_x  = sx;
        c.in.block_offset_y  = sy;
        c.in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565;
        c.out.buffer         = b->data;
        c.out.buffer_size    = b->data_size;
        c.out.pic_w          = b->header.stride / 2;
        c.out.pic_h          = b->header.h;
        c.out.block_offset_x = dx;
        c.out.block_offset_y = dy;
        c.out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565;
        c.rotation_angle     = PPA_SRM_ROTATION_ANGLE_0;
        c.scale_x = c.scale_y = 1.0f;
        c.mode                = PPA_TRANS_MODE_BLOCKING;
        result                = ppa_do_scale_rotate_mirror(u->srm, &c);
    }
    if (result != ESP_OK) {
        ++stats.errors;
        return false;
    }
    return true;
}

int32_t dispatch(lv_draw_unit_t* base, lv_layer_t* layer)
{
    auto* t = lv_draw_get_next_available_task(layer, nullptr, kUnitId);
    if (!t) return LV_DRAW_UNIT_IDLE;
    if (!lv_draw_layer_alloc_buf(layer)) return LV_DRAW_UNIT_IDLE;
    t->state           = LV_DRAW_TASK_STATE_IN_PROGRESS;
    base->target_layer = layer;
    base->clip_area    = &t->clip_area;
    std::uint64_t pixels{};
    const auto started = esp_timer_get_time();
    if (draw(reinterpret_cast<Unit*>(base), layer, t, pixels)) {
        const auto elapsed = std::uint64_t(esp_timer_get_time() - started);
        stats.elapsed_us += elapsed;
        stats.max_us = std::max(stats.max_us, elapsed);
        stats.pixels += pixels;
        if (t->type == LV_DRAW_TASK_TYPE_IMAGE) {
            ++stats.image_tasks;
            stats.image_us += elapsed;
        } else {
            ++stats.fill_tasks;
            stats.fill_us += elapsed;
        }
    } else {
        ++stats.fallbacks;
        // These helpers only require lv_draw_unit_t's target_layer/clip_area;
        // they do not cast it to lv_draw_sw_unit_t in local LVGL 9.2.
        if (t->type == LV_DRAW_TASK_TYPE_IMAGE)
            lv_draw_sw_image(base, static_cast<const lv_draw_image_dsc_t*>(t->draw_dsc), &t->area);
        else
            lv_draw_sw_fill(base, static_cast<lv_draw_fill_dsc_t*>(t->draw_dsc), &t->area);
    }
    t->state = LV_DRAW_TASK_STATE_READY;
    lv_draw_dispatch_request();
    return 1;
}

int32_t destroy(lv_draw_unit_t* base)
{
    auto* u = reinterpret_cast<Unit*>(base);
    if (u->copy) esp_async_fbcpy_uninstall(u->copy);
    if (u->copy_done) vSemaphoreDelete(u->copy_done);
    if (original_sw_dispatch && u->base.next) {
        u->base.next->dispatch_cb = original_sw_dispatch;
        original_sw_dispatch = nullptr;
    }
    ppa_unregister_client(u->srm);
    ppa_unregister_client(u->fill);
    unit              = nullptr;
    stats.initialized = false;
    return 0;
}
}  // namespace

bool init_ppa_draw()
{
    if (unit) return true;
    std::size_t alignment{};
    if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &alignment) != ESP_OK || !alignment) {
        ++stats.errors;
        return false;
    }
    stats.alignment = alignment;
    ppa_client_config_t config{};
    config.oper_type             = PPA_OPERATION_SRM;
    config.max_pending_trans_num = 1;
    config.data_burst_length     = PPA_DATA_BURST_LENGTH_64;
    ppa_client_handle_t srm{}, fill{};
    if (ppa_register_client(&config, &srm) != ESP_OK) {
        ++stats.errors;
        return false;
    }
    config.oper_type = PPA_OPERATION_FILL;
    if (ppa_register_client(&config, &fill) != ESP_OK) {
        ppa_unregister_client(srm);
        ++stats.errors;
        return false;
    }
    unit = static_cast<Unit*>(lv_draw_create_unit(sizeof(Unit)));
    if (!unit) {
        ppa_unregister_client(srm);
        ppa_unregister_client(fill);
        ++stats.errors;
        return false;
    }
    unit->srm              = srm;
    unit->fill             = fill;
    unit->copy_done = xSemaphoreCreateBinary();
    if (unit->copy_done) {
        esp_async_fbcpy_config_t copy_config{};
        if (esp_async_fbcpy_install(&copy_config, &unit->copy) != ESP_OK) {
            vSemaphoreDelete(unit->copy_done);
            unit->copy_done = nullptr;
        }
    }
    ESP_LOGI("scope-draw", "RGB565 image copy: %s", unit->copy ? "DMA2D" : "PPA fallback");
    unit->base.evaluate_cb = evaluate;
    unit->base.dispatch_cb = dispatch;
    unit->base.delete_cb   = destroy;
#if LV_USE_OS == LV_OS_NONE && LV_DRAW_SW_DRAW_UNIT_CNT == 1
    // Wrap the existing software unit without modifying vendored LVGL.
    if (!original_sw_dispatch && unit->base.next) {
        original_sw_dispatch = unit->base.next->dispatch_cb;
        unit->base.next->dispatch_cb = profile_sw_dispatch;
    }
#endif
    stats.initialized      = true;
    return true;
}
const PpaStats& ppa_stats()
{
    return stats;
}
}  // namespace vibescope
#else
namespace vibescope {
bool init_ppa_draw()
{
    return false;
}
const PpaStats& ppa_stats()
{
    static const PpaStats empty;
    return empty;
}
}  // namespace vibescope
#endif
