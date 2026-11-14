/**
 * @file hal_probe_cdc.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-10-11
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "hal/hal_esp32.h"

#include "esp_log.h"
#include "hal/probe_cdc_esp_idf.hpp"

void HalEsp32::probe_cdc_init()
{
    if (!vhm::probe::probe_cdc().begin())
        ESP_LOGE("hal_probe_cdc", "Failed to initialize VibraNode CDC transport");
}
