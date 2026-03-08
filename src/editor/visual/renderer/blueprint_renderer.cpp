#include "visual/renderer/blueprint_renderer.h"

void BlueprintRenderer::render(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                               Pt canvas_min, Pt canvas_max, VisualNodeCache& cache,
                               const std::vector<size_t>* selected_nodes,
                               std::optional<size_t> selected_wire,
                               const an24::Simulator<an24::JIT_Solver>* sim,
                               const std::string& group_id) {
    grid_renderer_.render(dl, vp, canvas_min, canvas_max);
    wire_renderer_.render(bp, dl, vp, canvas_min, cache, sim, selected_wire, group_id);
    node_renderer_.render(bp, dl, vp, canvas_min, cache, selected_nodes, group_id);
}

TooltipInfo BlueprintRenderer::detectTooltip(const Blueprint& bp, const Viewport& vp,
                                             Pt canvas_min, VisualNodeCache& cache,
                                             Pt world_pos,
                                             const an24::Simulator<an24::JIT_Solver>& sim,
                                             const std::string& group_id) const {
    return tooltip_detector_.detect(bp, vp, canvas_min, cache, world_pos, sim,
                                    wire_renderer_.polylines(), group_id);
}

void BlueprintRenderer::renderTooltip(IDrawList& dl, const TooltipInfo& tooltip) {
    TooltipDetector::renderTooltip(dl, tooltip);
}

void BlueprintRenderer::renderGrid(IDrawList& dl, const Viewport& vp, Pt canvas_min, Pt canvas_max) {
    GridRenderer grid;
    grid.render(dl, vp, canvas_min, canvas_max);
}
