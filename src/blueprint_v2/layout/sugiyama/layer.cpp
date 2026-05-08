#include "layer.h"
#include <algorithm>
#include <unordered_set>

namespace bp2::layout::sugiyama {

namespace {

/// DFS coloring for cycle detection.
enum class Color : uint8_t { White, Gray, Black };

/// Detect cycles via DFS, collect back-edges. Returns reversed adjacency
/// (i.e., for each back-edge, the direction is temporarily flipped in
/// the effective successors/predecessors).
struct CycleBreakResult {
    /// Back-edges that were detected (source→target where target is ancestor).
    std::vector<Graph::Edge> feedback_edges;
    /// Effective successors after reversing back-edges.
    std::unordered_map<core::InternedId, std::vector<core::InternedId>> effective_succ;
};

CycleBreakResult break_cycles(const Graph& graph) {
    CycleBreakResult result;

    // Build effective adjacency (mutable copy of successors).
    for (const auto& [id, succs] : graph.successors) {
        result.effective_succ[id] = std::vector<core::InternedId>(succs.begin(), succs.end());
    }

    std::unordered_map<core::InternedId, Color> color;
    std::vector<core::InternedId> dfs_stack;
    std::vector<size_t> iter_stack;

    // Find all nodes to visit.
    std::vector<core::InternedId> all_nodes;
    all_nodes.reserve(graph.nodes.size());
    for (const auto& n : graph.nodes) {
        all_nodes.push_back(n.id);
    }

    for (const auto& start : all_nodes) {
        if (color[start] != Color::White) continue;

        dfs_stack.push_back(start);
        iter_stack.push_back(0);

        while (!dfs_stack.empty()) {
            auto node = dfs_stack.back();
            auto& idx = iter_stack.back();

            if (color[node] == Color::White) {
                color[node] = Color::Gray;
            }

            auto& succs = result.effective_succ[node];
            bool found_unvisited = false;

            while (idx < succs.size()) {
                core::InternedId const next = succs[idx];
                idx++;

                if (color[next] == Color::Gray) {
                    // Back-edge detected: node → next where next is ancestor.
                    // Reverse this edge: remove node→next, add next→node.
                    result.feedback_edges.push_back({node, next});

                    // Remove next from effective successors of node.
                    succs.erase(succs.begin() + static_cast<ptrdiff_t>(idx) - 1);
                    idx--;

                    // Add node as effective successor of next (reversed edge).
                    // Guard against duplicates — the original graph may already
                    // contain next→node, so node could already be present.
                    auto& next_succs = result.effective_succ[next];
                    if (std::find(next_succs.begin(), next_succs.end(), node) == next_succs.end()) {
                        next_succs.push_back(node);
                    }

                    found_unvisited = false;
                    break;
                }

                if (color[next] == Color::White) {
                    found_unvisited = true;
                    dfs_stack.push_back(next);
                    iter_stack.push_back(0);
                    break;
                }
                // Color::Black → already processed, skip.
            }

            if (!found_unvisited && (iter_stack.empty() || idx >= succs.size())) {
                color[node] = Color::Black;
                dfs_stack.pop_back();
                iter_stack.pop_back();
            }
        }
    }

    return result;
}

/// Compute longest-path ranking on the DAG (after cycle breaking).
/// Sources (no predecessors in the effective graph) get rank 0.
/// Each node gets rank = max(predecessor ranks) + 1.
std::unordered_map<core::InternedId, int> longest_path_rank(
    const Graph& graph,
    const CycleBreakResult& cbr) {

    std::unordered_map<core::InternedId, int> rank;
    std::unordered_map<core::InternedId, int> in_degree;

    // Compute effective in-degree (predecessor count after cycle reversal).
    // Start with original predecessors.
    for (const auto& n : graph.nodes) {
        in_degree[n.id] = 0;
    }
    for (const auto& [src, succs] : cbr.effective_succ) {
        for (const auto& tgt : succs) {
            in_degree[tgt]++;
        }
    }

    // Kahn's algorithm for topological order with ranking.
    std::vector<core::InternedId> queue;
    for (const auto& n : graph.nodes) {
        if (in_degree[n.id] == 0) {
            rank[n.id] = 0;
            queue.push_back(n.id);
        }
    }

    size_t qi = 0;
    while (qi < queue.size()) {
        auto node = queue[qi++];
        int const node_rank = rank[node];

        auto it = cbr.effective_succ.find(node);
        if (it == cbr.effective_succ.end()) continue;

        for (const auto& succ : it->second) {
            int const candidate = node_rank + 1;
            auto& succ_rank = rank[succ];
            succ_rank = std::max(succ_rank, candidate);

            if (--in_degree[succ] == 0) {
                queue.push_back(succ);
            }
        }
    }

    return rank;
}

} // namespace

Layering assign_layers(const Graph& graph) {
    Layering result;

    if (graph.nodes.empty()) return result;

    // Step 1: Break cycles via DFS.
    auto cbr = break_cycles(graph);
    result.feedback_edges = std::move(cbr.feedback_edges);

    // Step 2: Compute longest-path ranking.
    auto rank = longest_path_rank(graph, cbr);

    // Isolated nodes get rank 0.
    for (const auto& id : graph.isolated) {
        rank[id] = 0;
    }

    // Step 3: Build layers from rank map.
    int max_rank = 0;
    for (const auto& [id, r] : rank) {
        max_rank = std::max(max_rank, r);
    }

    result.layers.resize(static_cast<size_t>(max_rank) + 1);
    result.rank = rank;

    // Sort nodes within each layer by id for deterministic ordering.
    // Crossing minimization will reorder later.
    struct RankEntry {
        core::InternedId id;
        int rank;
    };
    std::vector<RankEntry> entries;
    entries.reserve(graph.nodes.size());
    for (const auto& n : graph.nodes) {
        auto it = rank.find(n.id);
        if (it != rank.end()) {
            entries.push_back({n.id, it->second});
        }
    }
    // Stable sort by rank to preserve node order within same rank.
    std::stable_sort(entries.begin(), entries.end(),
                     [](const RankEntry& a, const RankEntry& b) { return a.rank < b.rank; });

    for (const auto& e : entries) {
        result.layers[static_cast<size_t>(e.rank)].push_back(e.id);
    }

    return result;
}

} // namespace bp2::layout::sugiyama
