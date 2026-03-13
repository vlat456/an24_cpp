#pragma once

#include "ui/renderer/idraw_list.h"
#include <string>

using ui::IDrawList;

/// Tooltip info for hovered elements (port/wire).
struct TooltipInfo {
    bool active = false;
    Pt screen_pos;
    std::string text;
    std::string label;
};
