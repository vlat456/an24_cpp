#pragma once

#include "data/pt.h"
#include "editor/router/crossings.h"
#include "viewport/viewport.h"
#include "visual/renderer/draw_list.h"
#include <vector>

namespace arc_draw {

void draw_jump_arc(IDrawList& dl, const WireCrossing& crossing,
                   Pt screen_pos, float arc_radius, uint32_t wire_color);

void draw_all_arcs(IDrawList& dl, const std::vector<WireCrossing>& crossings,
                   const Viewport& vp, Pt canvas_min, uint32_t wire_color);

}
