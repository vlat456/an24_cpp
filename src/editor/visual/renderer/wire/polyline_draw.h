#pragma once

#include "data/pt.h"
#include "editor/router/crossings.h"
#include "viewport/viewport.h"
#include "visual/renderer/draw_list.h"
#include <vector>

namespace polyline_draw {

struct CrossOnSeg {
    size_t seg_idx;
    float t;
    Pt pos;
    SegDir my_seg_dir;
};

std::vector<CrossOnSeg> classify_crossings_by_segment(
    const std::vector<WireCrossing>& crossings,
    const std::vector<Pt>& poly);

void draw_polyline_with_gaps(IDrawList& dl, const std::vector<Pt>& poly,
                             const std::vector<CrossOnSeg>& segs_crossings,
                             const Viewport& vp, Pt canvas_min,
                             uint32_t wire_color, float gap_radius);

}
