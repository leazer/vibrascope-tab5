/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <app.h>
#include <mooncake.h>
#include <memory>
#include <hal/hal.h>
#include "app_template/app_template.h"
#include "app_launcher/app_launcher.h"
#include "app_startup_anim/app_startup_anim.h"
#include "app_link_validation/app_link_validation.h"
#include "app_vibescope/app_vibescope.h"
/* Header files locator (Don't remove) */

// Start boot anim app and wait for it to finish
inline void on_startup_anim()
{
    auto app_id = mooncake::GetMooncake().installApp(std::make_unique<AppStartupAnim>());
    mooncake::GetMooncake().openApp(app_id);
    while (1) {
        // 必须复用主循环的泵：macOS/Windows 的 lv_timer_handler（含平台事件泵）
        // 也在 app::Update 里（见 app.cpp），动画期间若只泵 mooncake，动画全程不渲染
        app::Update();
        if (mooncake::GetMooncake().getAppCurrentState(app_id) == mooncake::AppAbility::StateSleeping) {
            break;
        }
        GetHAL()->delay(1);
    }
    mooncake::GetMooncake().uninstallApp(app_id);
}

/**
 * @brief App 安装回调
 *
 * @param mooncake
 */
inline void on_install_apps()
{
    // 安装 App
    // mooncake::GetMooncake().installApp(std::make_unique<AppTemplate>());
    // VibraScope is the active instrument UI. The factory launcher auto-opens
    // and keeps polling/updating every demo panel even behind another screen;
    // keep its implementation as a reference, without a second running app.
    mooncake::GetMooncake().installApp(std::make_unique<AppVibescope>());
    // mooncake::GetMooncake().installApp(std::make_unique<AppLinkValidation>());
    /* Install app locator (Don't remove) */
}
