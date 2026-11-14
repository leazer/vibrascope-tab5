/**
 * @file panic_screen.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-10
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <cstddef>

void tab5_register_panic_framebuffer(const void* framebuffer, std::size_t bytes);
