#include "auto_layout.h"
#include "sugiyama/graph.h"
#include "sugiyama/layer.h"
#include "sugiyama/crossing.h"
#include "sugiyama/coordinates.h"
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

    // Build batch update list — O(N) single pass.
    std::vector<NodePositionUpdate> updates;
    updates.reserve(layout.positions.size());
    for (const auto& node : bp.nodes()) {
        auto it = layout.positions.find(node.semantic.id);
        if (it == layout.positions.end()) continue;

        // Skip if position unchanged.
        if (node.layout.x == it->second.x && node.layout.y == it->second.y) continue;

        updates.push_back({node.semantic.id, it->second.x, it->second.y});
    }

    if (updates.empty()) return bp;
    return bp.with_updated_positions(updates);
}

} // namespace bp2::layout
