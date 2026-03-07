#pragma once

#include "visual/renderer/draw_list.h"
#include "visual/renderer/wire_renderer.h"
#include "visual/renderer/node_renderer.h"
#include "visual/renderer/tooltip_detector.h"
#include "visual/renderer/grid_renderer.h"
#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "visual/node/node.h"
#include "jit_solver/simulator.h"
#include <optional>
#include <vector>

/// Composes WireRenderer, NodeRenderer, TooltipDetector, GridRenderer
/// into a single rendering pipeline for a blueprint view.
class BlueprintRenderer {
public:
    /// Render grid + wires + nodes.
    void render(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                Pt canvas_min, Pt canvas_max, VisualNodeCache& cache,
                const std::vector<size_t>* selected_nodes = nullptr,
                std::optional<size_t> selected_wire = std::nullopt,
                const an24::Simulator<an24::JIT_Solver>* sim = nullptr);

    /// Detect tooltip at hover position (call after render()).
    TooltipInfo detectTooltip(const Blueprint& bp, const Viewport& vp,
                              Pt canvas_min, VisualNodeCache& cache,
                              Pt world_pos,
                              const an24::Simulator<an24::JIT_Solver>& sim) const;

    /// Render a tooltip box.
    static void renderTooltip(IDrawList& dl, const TooltipInfo& tooltip);

    /// Render grid only (convenience to call separately from render()).
    static void renderGrid(IDrawList& dl, const Viewport& vp, Pt canvas_min, Pt canvas_max);

private:
    WireRenderer wire_renderer_;
    NodeRenderer node_renderer_;
    GridRenderer grid_renderer_;
    TooltipDetector tooltip_detector_;
};
