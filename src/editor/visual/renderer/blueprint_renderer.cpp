#include "visual/renderer/blueprint_renderer.h"


void BlueprintRenderer::render(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                               Pt canvas_min, Pt canvas_max, VisualNodeCache& cache,
                               const std::vector<size_t>* selected_nodes,
                               std::optional<size_t> selected_wire,
                               const Simulator<JIT_Solver>* sim,
                               std::optional<size_t> hovered_wire,
                               const std::string& group_id) {
    grid_renderer_.render(dl, vp, canvas_min, canvas_max);
    node_renderer_.renderGroups(bp, dl, vp, canvas_min, cache, selected_nodes, group_id);
    node_renderer_.renderTexts(bp, dl, vp, canvas_min, cache, selected_nodes, group_id);
    wire_renderer_.render(bp, dl, vp, canvas_min, cache, sim, selected_wire, hovered_wire, group_id);
    node_renderer_.renderNodes(bp, dl, vp, canvas_min, cache, selected_nodes, group_id);
    node_renderer_.renderResizeHandles(bp, dl, vp, canvas_min, cache, selected_nodes, group_id);
}

TooltipInfo BlueprintRenderer::detectTooltip(const Blueprint& bp, const Viewport& vp,
                                             Pt canvas_min, VisualNodeCache& cache,
                                             Pt world_pos,
                                             const Simulator<JIT_Solver>& sim,
                                             const editor_spatial::SpatialGrid& grid,
                                             const std::string& group_id) const {
    return tooltip_detector_.detect(bp, vp, canvas_min, cache, world_pos, sim,
                                    grid, group_id);
}

void BlueprintRenderer::renderTooltip(IDrawList& dl, const TooltipInfo& tooltip) {
    TooltipDetector::renderTooltip(dl, tooltip);
}

void BlueprintRenderer::renderGrid(IDrawList& dl, const Viewport& vp, Pt canvas_min, Pt canvas_max) {
    GridRenderer grid;
    grid.render(dl, vp, canvas_min, canvas_max);
}
