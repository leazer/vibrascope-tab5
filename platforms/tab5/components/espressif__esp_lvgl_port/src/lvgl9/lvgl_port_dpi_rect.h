/**
 * @file lvgl_port_dpi_rect.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-04
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int x, y, width, height;
} lvgl_port_dpi_rect_t;

/* LVGL ROTATION_90 maps landscape coordinates into native portrait storage.
 * Validate before computing destination offsets; all rectangles are inclusive. */
static inline bool lvgl_port_dpi_rotate90_rect(int native_width, int native_height,
                                               int x1, int y1, int x2, int y2,
                                               lvgl_port_dpi_rect_t *out)
{
    if (!out || native_width <= 0 || native_height <= 0 || x1 < 0 || y1 < 0 ||
        x2 < x1 || y2 < y1 || x2 >= native_height || y2 >= native_width) return false;
    out->x = y1;
    out->y = native_height - x2 - 1;
    out->width = y2 - y1 + 1;
    out->height = x2 - x1 + 1;
    return true;
}
