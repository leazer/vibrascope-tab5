/**
 * @file app_vibescope.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <mooncake.h>

#include "view/view.h"
#include <memory>

class AppVibescope : public mooncake::AppAbility {
public:
    AppVibescope();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<vibescope_view::VibescopeView> _view;
};
