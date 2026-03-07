#pragma once

#include "visual/renderer/draw_list.h"
#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "visual/node/node.h"
#include "jit_solver/simulator.h"
#include <vector>

/// Detects tooltip info when hovering over ports or wire segments.
/// Requires polylines from WireRenderer.
class TooltipDetector {
public:
    /// Detect tooltip at world_pos. Returns active TooltipInfo or inactive default.
    TooltipInfo detect(const Blueprint& bp, const Viewport& vp,
                       Pt canvas_min, VisualNodeCache& cache,
                       Pt world_pos,
                       const an24::Simulator<an24::JIT_Solver>& sim,
                       const std::vector<std::vector<Pt>>& polylines) const;

    /// Render a tooltip box.
    static void renderTooltip(IDrawList& dl, const TooltipInfo& tooltip);
};
