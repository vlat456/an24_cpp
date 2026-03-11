#pragma once

#include "editor/document.h"
#include "editor/data/pt.h"
#include <functional>

namespace an24 {

class WindowSystem;

/// Document tabs renderer
class DocumentTabs {
public:
    struct Result {
        Document* close_requested = nullptr;
    };

    /// Render tabs and return result
    Result render(WindowSystem& ws);

    /// Get current tab bar height (for layout)
    float tabBarHeight() const { return tab_bar_height_; }

private:
    float tab_bar_height_ = 0.0f;
};

} // namespace an24
