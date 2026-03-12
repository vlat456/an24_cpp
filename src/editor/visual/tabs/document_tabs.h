#pragma once

#include "editor/document.h"
#include "editor/window_system.h"
#include <functional>

namespace an24 {

/// Document tabs renderer
class DocumentTabs {
public:
    struct Result {
        Document* close_requested = nullptr;
    };

    Result render(::WindowSystem& ws);
    float tabBarHeight() const { return tab_bar_height_; }

private:
    float tab_bar_height_ = 0.0f;
};

} // namespace an24
