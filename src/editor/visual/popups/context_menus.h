#pragma once

#include "editor/window_system.h"
#include <functional>


/// Context menus for adding components and node actions.
class ContextMenus {
public:
    void renderAddComponent(WindowSystem& ws);
    void renderNodeContext(WindowSystem& ws);
};

