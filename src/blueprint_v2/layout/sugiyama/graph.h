#pragma once

#include "core/strings/interned_id.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bp2 {
class Blueprint;
} // namespace bp2

namespace bp2::layout::sugiyama {

/// Lightweight adjacency representation extracted from Blueprint.
/// Node identity: InternedId. Edge = (source_node, target_node).
/// Multi-edges between the same node pair are collapsed — the algorithm
/// positions nodes, not individual wires.
struct Graph {
    struct Node {
        core::InternedId id;
        float width;   ///< For spacing computation.
        float height;   ///< For spacing computation.
    };

    struct Edge {
        core::InternedId source;
        core::InternedId target;
    };

    std::vector<Node> nodes;
    std::vector<Edge> edges;

    /// Node → successor set (forward adjacency).
    std::unordered_map<core::InternedId, std::unordered_set<core::InternedId>> successors;
    /// Node → predecessor set (reverse adjacency).
    std::unordered_map<core::InternedId, std::unordered_set<core::InternedId>> predecessors;

    /// Nodes with no edges in or out.
    std::unordered_set<core::InternedId> isolated;
};

/// Extract a Sugiyama graph from a Blueprint.
/// Uses LayoutData width/height if present, otherwise defaults.
Graph extract_graph(const Blueprint& bp,
                    float default_width = 120.0f,
                    float default_height = 80.0f);

} // namespace bp2::layout::sugiyama
