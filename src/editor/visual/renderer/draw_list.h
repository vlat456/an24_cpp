#pragma once

#include "ui/renderer/idraw_list.h"
#include <string>

/// Tooltip info for hovered elements (port/wire).
struct TooltipInfo {
    bool active = false;
    ui::Pt screen_pos;
    std::string text;
    std::string label;
};
