/**
 * @file panic_screen.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-10
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "panic_screen.h"
#include <bsp/display.h>
#include <cstdint>
#include <esp_attr.h>
#include <esp_memory_utils.h>
#include <esp_private/cache_utils.h>
#include <esp_private/dw_gdma.h>
#include <esp_private/panic_internal.h>
#include <esp_private/panic_reason.h>
#include <esp_rom_sys.h>
#include <hal/cache_hal.h>
#include <hal/dw_gdma_ll.h>
#include <hal/usb_serial_jtag_ll.h>
#include <riscv/rvruntime-frames.h>

namespace {
constexpr unsigned kNativeWidth = BSP_LCD_H_RES;
constexpr unsigned kNativeHeight = BSP_LCD_V_RES;
constexpr unsigned kPixels = kNativeWidth * kNativeHeight;
constexpr std::uint16_t kBlue = 0x039A, kWhite = 0xFFFF;
constexpr unsigned kStackWords = 24;

std::uint16_t* volatile framebuffer;
dw_gdma_channel_handle_t lcd_dma;
void* lcd_dma_context;
int lcd_dma_index;
const RvExcFrame* crash_frame;
const char* crash_reason;
bool drawing;

// 5x7 font encoded as five columns, LSB at the top. A tiny fixed font
// lets the crash handler avoid the display driver's locks and allocations.
constexpr std::uint8_t glyphs[][5] = {
    {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00}, // 0 1
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e},
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36}, // A B
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
    {0x00,0x36,0x36,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, // : -
    {0x20,0x10,0x08,0x04,0x02}, {0x00,0x00,0x60,0x60,0x00}, // / .
    {0x00,0x1c,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1c,0x00}, // ( )
    {0x40,0x40,0x40,0x40,0x40}, {0x00,0x40,0x40,0x00,0x00}, // _ ,
};

void character(unsigned x, unsigned y, char ch, unsigned scale)
{
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    unsigned index;
    if (ch >= '0' && ch <= '9') index = ch - '0';
    else if (ch >= 'A' && ch <= 'Z') index = ch - 'A' + 10;
    else {
        constexpr char punctuation[] = ":-/.()_,";
        unsigned i = 0;
        while (punctuation[i] && punctuation[i] != ch) ++i;
        if (!punctuation[i]) return;
        index = 36 + i;
    }
    // The TAB5 panel scans in portrait. Match LVGL's rotation-90 mapping.
    for (unsigned col = 0; col < 5; ++col)
        for (unsigned row = 0; row < 7; ++row) {
            if (!(glyphs[index][col] & (1U << row))) continue;
            for (unsigned dx = 0; dx < scale; ++dx)
                for (unsigned dy = 0; dy < scale; ++dy) {
                    const unsigned px = x + col * scale + dx;
                    const unsigned py = y + row * scale + dy;
                    framebuffer[(kNativeHeight - 1 - px) * kNativeWidth + py] = kWhite;
                }
        }
}

void text(unsigned x, unsigned y, const char* line, unsigned scale)
{
    for (; x + 6 * scale <= kNativeHeight && *line && *line != '\r' && *line != '\n'; ++line) {
        character(x, y, *line, scale);
        x += 6 * scale;
    }
}

void hex(unsigned x, unsigned y, std::uint32_t value)
{
    for (int shift = 28; shift >= 0; shift -= 4, x += 18)
        character(x, y, "0123456789ABCDEF"[(value >> shift) & 15U], 3);
}

void IRAM_ATTR flush_panic_console()
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
    // The panic writer fills the USB FIFO a byte at a time. A short final
    // packet must be submitted explicitly because no console task is running.
    usb_serial_jtag_ll_txfifo_flush();
    for (unsigned attempt = 0; attempt < 50; ++attempt) {
        if (usb_serial_jtag_ll_txfifo_writable()) {
            usb_serial_jtag_ll_txfifo_flush();
            break;
        }
        esp_rom_delay_us(100);
    }
#endif
}
} // namespace

void tab5_register_panic_framebuffer(const void* pixels, std::size_t bytes)
{
    if (bytes >= kPixels * sizeof(std::uint16_t))
        framebuffer = const_cast<std::uint16_t*>(static_cast<const std::uint16_t*>(pixels));
}

extern "C" {
void __real_esp_panic_handler(panic_info_t* info);
void __real_esp_system_reset_modules_on_exit();
esp_err_t __real_dw_gdma_channel_register_event_callbacks(dw_gdma_channel_handle_t channel,
                                                         dw_gdma_event_callbacks_t* callbacks, void* context);
bool mipi_dsi_dma_trans_done_cb(dw_gdma_channel_handle_t channel, const dw_gdma_trans_done_event_data_t* event,
                               void* context);
#if CONFIG_ESP_SYSTEM_HW_STACK_GUARD
void esp_hw_stack_guard_monitor_stop();
#endif

esp_err_t __wrap_dw_gdma_channel_register_event_callbacks(dw_gdma_channel_handle_t channel,
                                                         dw_gdma_event_callbacks_t* callbacks, void* context)
{
    const auto result = __real_dw_gdma_channel_register_event_callbacks(channel, callbacks, context);
    if (result == ESP_OK && callbacks && callbacks->on_full_trans_done == mipi_dsi_dma_trans_done_cb &&
        dw_gdma_channel_get_id(channel, &lcd_dma_index) == ESP_OK) {
        lcd_dma_context = context;
        lcd_dma = channel;
    }
    return result;
}

void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t* info)
{
    const auto* frame = static_cast<const RvExcFrame*>(info->frame);
    if (!drawing && framebuffer && lcd_dma && frame && spi_flash_cache_enabled() &&
        !(info->pseudo_excause && frame->mcause == PANIC_RSN_CACHEERR)) {
        crash_reason = g_panic_abort ? g_panic_abort_details : info->reason;
        crash_frame = frame;
    }
    __real_esp_panic_handler(info);
    crash_frame = nullptr; // IDF may return when an on-chip debugger is attached.
}

// The exception may have been caused by an exhausted task stack. Draw on
// dedicated internal RAM after IDF has stopped both cores and watchdogs.
alignas(16) DRAM_ATTR std::uint8_t tab5_panic_stack[1536];
[[gnu::used]] bool tab5_draw_panic()
{
    auto* const pixels = framebuffer;
    for (unsigned i = 0; i < kPixels; ++i) pixels[i] = kBlue;
    text(76, 46, ":(", 10);
    text(76, 139, "VIBRANODE / DEVICE HALTED", 5);
    text(76, 192, "THE DEVICE STOPPED DUE TO A FATAL ERROR.", 4);
    text(76, 252, "STOP CODE:", 3);
    text(280, 252, crash_reason ? crash_reason : "FATAL EXCEPTION", 3);
    const auto& frame = *crash_frame;
    text(76, 294, "PC:", 3);
    hex(145, 294, frame.mepc);
    text(349, 294, "RA:", 3);
    hex(418, 294, frame.ra);
    text(622, 294, "SP:", 3);
    hex(691, 294, frame.sp);
    text(895, 294, "CAUSE:", 3);
    hex(1022, 294, frame.mcause);

    text(76, 347, "STACK MEMORY (ADDRESS / WORDS)", 3);
    const auto sp = static_cast<std::uintptr_t>(frame.sp);
    const auto* stack = reinterpret_cast<const volatile std::uint32_t*>(sp);
    if ((sp & 3U) == 0 && esp_ptr_in_dram(reinterpret_cast<const void*>(sp)) &&
        esp_ptr_in_dram(reinterpret_cast<const void*>(sp + kStackWords * sizeof(std::uint32_t) - 1))) {
        for (unsigned i = 0; i < kStackWords; i += 6) {
            const unsigned y = 386 + i / 6 * 34;
            hex(76, y, sp + i * sizeof(std::uint32_t));
            for (unsigned j = 0; j < 6; ++j) hex(244 + j * 160, y, stack[i + j]);
        }
    } else {
        text(76, 386, "STACK POINTER OUTSIDE INTERNAL RAM", 3);
    }
    text(76, 573, "FULL REGISTER AND STACK DUMP: USB SERIAL", 3);
    text(76, 619, "PRESS RESET OR POWER CYCLE TO RESTART.", 4);
    if (!cache_hal_writeback_addr(reinterpret_cast<std::uintptr_t>(pixels), kPixels * 2)) return false;

    // The LCD driver submits one frame per DMA-complete interrupt. Interrupts
    // are unavailable after panic, so service only this channel by polling.
    auto* dma = DW_GDMA_LL_GET_HW(0);
    unsigned submitted = 0;
    while (true) {
        if (dma->chen0.val & (1U << lcd_dma_index)) continue;
        const auto status = dw_gdma_ll_channel_get_intr_status(dma, lcd_dma_index);
        dw_gdma_ll_channel_clear_intr(dma, lcd_dma_index, status);
        mipi_dsi_dma_trans_done_cb(lcd_dma, nullptr, lcd_dma_context);
        if (submitted < 2 && ++submitted == 2) {
            panic_print_str("TAB5 panic screen ready.\r\n");
            flush_panic_console();
        }
    }
}

[[gnu::naked, gnu::noinline]] bool tab5_draw_panic_on_stack()
{
    asm volatile(
        "mv t0, sp\n"
        "la sp, tab5_panic_stack\n"
        "addi sp, sp, 1520\n"
        "sw t0, 0(sp)\n"
        "sw ra, 4(sp)\n"
        "call tab5_draw_panic\n"
        "lw ra, 4(sp)\n"
        "lw sp, 0(sp)\n"
        "ret\n");
}

void IRAM_ATTR __wrap_esp_system_reset_modules_on_exit()
{
    if (crash_frame && !drawing && spi_flash_cache_enabled()) {
        drawing = true;
#if CONFIG_ESP_SYSTEM_HW_STACK_GUARD
        esp_hw_stack_guard_monitor_stop();
#endif
        tab5_draw_panic_on_stack(); // Returns only if display writeback failed.
    }
    flush_panic_console();
    __real_esp_system_reset_modules_on_exit();
}
} // extern "C"
