#include "auto_layout.h"
#include "sugiyama/graph.h"
#include "sugiyama/layer.h"
#include "sugiyama/crossing.h"
#include "sugiyama/coordinates.h"
#include "blueprint_v2/blueprint/blueprint_replace.h"
#include <cmath>

namespace bp2::layout {

namespace {

/// Snap a coordinate to the layout grid.
float snap_to_grid(float v, float grid) {
    if (grid < 1e-6f) return v;
    return std::round(v / grid) * grid;
}

} // namespace

LayoutResult compute_layout(const Blueprint& bp, const LayoutOptions& options) {
    LayoutResult result;
    if (bp.nodes().empty()) return result;

    // Phase 1: Extract graph.
    auto graph = sugiyama::extract_graph(bp, options.default_node_width, options.default_node_height);

    // Phase 2: Assign layers (with cycle breaking).
    auto layering = sugiyama::assign_layers(graph);

    // Phase 3: Minimize crossings.
    sugiyama::minimize_crossings(graph, layering, options.crossing_minimization_sweeps);

    // Phase 4: Assign coordinates.
    sugiyama::Spacing spacing{
        options.horizontal_spacing,
        options.vertical_spacing,
        options.margin_x,
        options.margin_y};
    auto coords = sugiyama::assign_coordinates(graph, layering, spacing);

    // Phase 5: Snap positions to layout grid.
    for (const auto& [id, pos] : coords) {
        result.positions[id] = {
            snap_to_grid(pos.x, options.snap_grid),
            snap_to_grid(pos.y, options.snap_grid)
        };
    }

    return result;
}

Blueprint apply_layout(const Blueprint& bp, const LayoutOptions& options) {
    auto layout = compute_layout(bp, options);
    if (layout.positions.empty()) return bp;

    // Batch-update: apply all positions in a single pass through replace_node.
    // Each call is O(N) but total is O(N²) for N nodes. For editor blueprints
    // (typically 10-100 nodes) this is negligible.
    Blueprint result = bp;
    for (const auto& node : bp.nodes()) {
        auto it = layout.positions.find(node.semantic.id);
        if (it == layout.positions.end()) continue;

        float new_x = it->second.x;
        float new_y = it->second.y;

        if (node.layout.x == new_x && node.layout.y == new_y) continue;

        auto updated = node;
        updated.layout.x = new_x;
        updated.layout.y = new_y;
        result = bp2::replace_node_preserve_order(result, std::move(updated));
    }

    return result;
}

} // namespace bp2::layout
