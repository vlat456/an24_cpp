#include "crossing.h"
#include <algorithm>
#include <numeric>

namespace bp2::layout::sugiyama {

namespace {

/// Build a map from node id → its position, for nodes in a single layer only.
std::unordered_map<core::InternedId, int> build_position_map(const Layer& layer) {
    std::unordered_map<core::InternedId, int> pos;
    for (int i = 0; i < static_cast<int>(layer.size()); ++i) {
        pos[layer[i]] = i;
    }
    return pos;
}

/// Compute barycenter for each node in a layer based on neighbors
/// in the adjacent layer. Returns per-node barycenter value.
/// Nodes with no neighbors get their current position as barycenter.
std::vector<double> compute_barycenters(
    const Layer& layer,
    const std::unordered_map<core::InternedId, int>& neighbor_pos,
    const std::unordered_map<core::InternedId, std::unordered_set<core::InternedId>>& adjacency) {

    std::vector<double> barycenters(layer.size());
    for (size_t i = 0; i < layer.size(); ++i) {
        auto it = adjacency.find(layer[i]);
        if (it != adjacency.end() && !it->second.empty()) {
            double sum = 0.0;
            int count = 0;
            for (const auto& nb : it->second) {
                auto pit = neighbor_pos.find(nb);
                if (pit != neighbor_pos.end()) {
                    sum += static_cast<double>(pit->second);
                    count++;
                }
            }
            barycenters[i] = (count > 0) ? (sum / count) : static_cast<double>(i);
        } else {
            // No neighbors in adjacent layer — keep current position.
            barycenters[i] = static_cast<double>(i);
        }
    }
    return barycenters;
}

/// Sort a layer by barycenter values (stable for determinism).
void sort_layer_by_barycenter(Layer& layer, const std::vector<double>& barycenters) {
    // Build index array, sort by barycenter, then reorder.
    std::vector<size_t> indices(layer.size());
    std::iota(indices.begin(), indices.end(), 0u);
    std::stable_sort(indices.begin(), indices.end(),
                     [&barycenters](size_t a, size_t b) {
                         return barycenters[a] < barycenters[b];
                     });

    Layer sorted;
    sorted.reserve(layer.size());
    for (auto idx : indices) {
        sorted.push_back(layer[idx]);
    }
    layer = std::move(sorted);
}

} // namespace

void minimize_crossings(const Graph& graph, Layering& layering, int num_sweeps) {
    if (layering.layers.size() < 2) return;

    // Build full adjacency (both directions) from original graph.
    // Crossing minimization treats all connections as undirected —
    // both forward and backward adjacency are populated for each edge,
    // so feedback edge direction doesn't affect the result.
    std::unordered_map<core::InternedId, std::unordered_set<core::InternedId>> forward_adj;
    std::unordered_map<core::InternedId, std::unordered_set<core::InternedId>> backward_adj;

    for (const auto& edge : graph.edges) {
        forward_adj[edge.source].insert(edge.target);
        backward_adj[edge.target].insert(edge.source);
    }
    // Feedback edges go "backwards" visually but are stored as original direction.
    // For crossing minimization, treat them as connections between their endpoints.
    for (const auto& edge : layering.feedback_edges) {
        forward_adj[edge.source].insert(edge.target);
        backward_adj[edge.target].insert(edge.source);
    }

    for (int sweep = 0; sweep < num_sweeps; ++sweep) {
        // -- Left-to-right pass: sort each layer by barycenter of left neighbors --
        for (size_t li = 1; li < layering.layers.size(); ++li) {
            auto pos_map = build_position_map(layering.layers[li - 1]);
            auto bary = compute_barycenters(layering.layers[li], pos_map, backward_adj);
            sort_layer_by_barycenter(layering.layers[li], bary);
        }

        // -- Right-to-left pass: sort each layer by barycenter of right neighbors --
        for (int li = static_cast<int>(layering.layers.size()) - 2; li >= 0; --li) {
            auto pos_map = build_position_map(layering.layers[li + 1]);
            auto bary = compute_barycenters(layering.layers[li], pos_map, forward_adj);
            sort_layer_by_barycenter(layering.layers[li], bary);
        }
    }
}

} // namespace bp2::layout::sugiyama
