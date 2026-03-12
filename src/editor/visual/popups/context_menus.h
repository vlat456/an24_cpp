#pragma once

#include "editor/window_system.h"
#include <functional>

namespace an24 {

/// Context menus for adding components and node actions.
class ContextMenus {
public:
    void renderAddComponent(WindowSystem& ws);
    void renderNodeContext(WindowSystem& ws);
};

} // namespace an24
