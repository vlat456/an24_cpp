#pragma once

#include "graph.h"
#include <unordered_map>
#include <vector>

namespace bp2::layout::sugiyama {

/// Each layer is an ordered list of node ids.
using Layer = std::vector<core::InternedId>;

/// Result of layer assignment.
struct Layering {
    /// layers[0] = leftmost column (sources), layers[N-1] = rightmost (sinks).
    std::vector<Layer> layers;

    /// Rank of each node (layer index). Needed by later phases.
    std::unordered_map<core::InternedId, int> rank;

    /// Back-edges reversed to break cycles (feedback connections).
    std::vector<Graph::Edge> feedback_edges;
};

/// Assign layers using longest-path ranking with DFS cycle breaking.
///
/// Algorithm:
/// 1. DFS from all nodes to detect cycles; reverse back-edges
/// 2. Longest-path ranking: sources get rank 0, others get max(predecessor rank) + 1
/// 3. Isolated nodes placed in layer 0
Layering assign_layers(const Graph& graph);

} // namespace bp2::layout::sugiyama
