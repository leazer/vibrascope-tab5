/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app.h"
#include "hal/hal.h"
#include "apps/app_installer.h"
#include <mooncake.h>
#include <mooncake_log.h>
#include <string>
#include <thread>

using namespace mooncake;

static const std::string _tag = "app";

void app::Init(InitCallback_t callback)
{
    mclog::tagInfo(_tag, "init");

    mclog::tagInfo(_tag, "hal injection");
    if (callback.onHalInjection) {
        callback.onHalInjection();
    }

    GetMooncake();

    on_startup_anim();
    on_install_apps();
}

void app::Update()
{
    // Mooncake 在 app_main 任务里直接操作 LVGL，而 esp_lvgl_port 的渲染跑在另一个任务。
    // 不加锁会触发 "Invalidate area is not allowed during rendering" 断言，
    // 并可能损坏非线程安全的 LVGL 内部状态（实板已复现崩溃）。
    // esp_lvgl_port 用递归互斥锁，桌面平台 lvglLock 为空实现，均不影响嵌套调用。
    LvglLockGuard lock;
    GetMooncake().update();

#if defined(__APPLE__) && defined(__MACH__) || defined(_WIN32)
    // 'nextEventMatchingMask should only be called from the Main Thread!'
    auto time_till_next = lv_timer_handler();
    std::this_thread::sleep_for(std::chrono::milliseconds(time_till_next));
#endif
}

bool app::IsDone()
{
    return false;
}

void app::Destroy()
{
    DestroyMooncake();
    hal::Destroy();
}
