/**
 * @file view.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <memory>

namespace vibescope_view {
class VibescopeView {
public:
    VibescopeView();
    ~VibescopeView();
    void init();
    void update();

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
}  // namespace vibescope_view
