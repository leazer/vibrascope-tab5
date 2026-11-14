/**
 * @file tab5_tusb_config.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-08
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#define CFG_TUSB_OS OPT_OS_FREERTOS
#define CFG_TUD_ENABLED 1
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_MSC 1
#define CFG_TUD_MSC_EP_BUFSIZE 4096
#define CFG_TUD_DWC2_DMA_ENABLE 0
