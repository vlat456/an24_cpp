#include "visual/renderer/node_renderer.h"

void NodeRenderer::render(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                          Pt canvas_min, VisualNodeCache& cache,
                          const std::vector<size_t>* selected_nodes) {
    size_t node_idx = 0;
    for (const auto& n : bp.nodes) {
        auto* visual = cache.getOrCreate(n, bp.wires);

        if (!visual->isVisible()) {
            node_idx++;
            continue;
        }

        bool is_selected = false;
        if (selected_nodes) {
            for (size_t idx : *selected_nodes) {
                if (idx == node_idx) { is_selected = true; break; }
            }
        }

        visual->render(&dl, vp, canvas_min, is_selected);
        node_idx++;
    }
}
