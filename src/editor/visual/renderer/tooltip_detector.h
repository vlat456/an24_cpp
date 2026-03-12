#pragma once

#include "visual/renderer/draw_list.h"
#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "visual/node/visual_node_cache.h"
#include "visual/spatial/grid.h"
#include "jit_solver/simulator.h"
#include <vector>

/// Detects tooltip info when hovering over ports or wire segments.
/// Uses SpatialGrid for O(1) candidate lookup instead of scanning all entities.
class TooltipDetector {
public:
    /// Detect tooltip at world_pos. Returns active TooltipInfo or inactive default.
    TooltipInfo detect(const Blueprint& bp, const Viewport& vp,
                       Pt canvas_min, VisualNodeCache& cache,
                       Pt world_pos,
                       const Simulator<JIT_Solver>& sim,
                       const editor_spatial::SpatialGrid& grid,
                       const std::string& group_id = "") const;

    /// Render a tooltip box.
    static void renderTooltip(IDrawList& dl, const TooltipInfo& tooltip);
};
