#pragma once

#include "visual/renderer/draw_list.h"
#include "viewport/viewport.h"

using ui::IDrawList;
using ui::Pt;

/// Renders the background grid.
class GridRenderer {
public:
    void render(IDrawList& dl, const Viewport& vp, Pt canvas_min, Pt canvas_max) const;
};
