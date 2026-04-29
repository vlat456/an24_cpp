#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include <unordered_map>

namespace bp2::layout {

/// Configuration for the auto-layout algorithm.
struct LayoutOptions {
    float horizontal_spacing = 250.0f;   ///< Between layer centers.
    float vertical_spacing = 100.0f;     ///< Between nodes within a layer.
    float margin_x = 50.0f;             ///< Left margin.
    float margin_y = 50.0f;             ///< Top margin.
    float default_node_width = 120.0f;   ///< Fallback node width.
    float default_node_height = 80.0f;   ///< Fallback node height.
    float snap_grid = 16.0f;            ///< Snap output positions to this grid (0 = no snap).
    int crossing_minimization_sweeps = 4; ///< Number of barycenter sweeps.
};

struct LayoutResult {
    struct Position {
        float x;
        float y;
    };
    std::unordered_map<core::InternedId, Position> positions;
};

/// Compute layout positions for all nodes in a blueprint.
/// Pure function: no side effects, no editor/UI dependency.
///
/// Pipeline: extract_graph → assign_layers → minimize_crossings → assign_coordinates
LayoutResult compute_layout(const bp2::Blueprint& bp, const LayoutOptions& options = {});

/// Apply layout to a blueprint, producing a new blueprint with updated node positions.
/// Returns the input blueprint unchanged if layout produces no changes.
bp2::Blueprint apply_layout(const bp2::Blueprint& bp, const LayoutOptions& options = {});

} // namespace bp2::layout
