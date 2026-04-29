#pragma once

#include "graph.h"
#include "layer.h"
#include <unordered_map>

namespace bp2::layout::sugiyama {

struct Spacing {
    float horizontal = 250.0f;  ///< Between layer centers (left→right).
    float vertical = 100.0f;    ///< Between nodes within a layer (top→bottom).
    float margin_x = 50.0f;     ///< Left margin for first layer.
    float margin_y = 50.0f;     ///< Top margin for first node in each layer.
};

struct NodePosition {
    float x;
    float y;
};

/// Assign pixel coordinates to each node.
///
/// V1: Simple even spacing. Layer index determines x-coordinate,
/// position within layer determines y-coordinate. Node heights
/// are accounted for to avoid overlaps.
///
/// @return Map from node id to computed (x, y) position.
std::unordered_map<core::InternedId, NodePosition> assign_coordinates(
    const Graph& graph,
    const Layering& layering,
    const Spacing& spacing = {});

} // namespace bp2::layout::sugiyama
