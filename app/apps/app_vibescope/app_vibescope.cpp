/**
 * @file app_vibescope.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2026
 *
 */
    #include "app_vibescope.h"
#include <mooncake_log.h>

AppVibescope::AppVibescope()
{
    setAppInfo().name = "VibraScope";
}

void AppVibescope::onCreate()
{
    open();
}

void AppVibescope::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    _view = std::make_unique<vibescope_view::VibescopeView>();
    _view->init();
}

void AppVibescope::onRunning()
{
    if (_view) _view->update();
}

void AppVibescope::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    _view.reset();
}
