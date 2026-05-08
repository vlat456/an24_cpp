#include "graph.h"
#include "blueprint_v2/blueprint/blueprint.h"

namespace bp2::layout::sugiyama {

Graph extract_graph(const Blueprint& bp, float default_width, float default_height) {
    Graph g;

    // -- Nodes --
    g.nodes.reserve(bp.nodes().size());
    for (const auto& node : bp.nodes()) {
        float const w = node.layout.width.value_or(default_width);
        float const h = node.layout.height.value_or(default_height);
        g.nodes.push_back({node.semantic.id, w, h});
    }

    // -- Edges from wires (collapse multi-edges) --
    for (const auto& wire : bp.wires()) {
        core::InternedId const src = wire.source.node;
        core::InternedId const tgt = wire.target.node;
        if (src.empty() || tgt.empty() || src == tgt) continue;

        // Only insert if this edge doesn't already exist.
        auto& succs = g.successors[src];
        if (succs.insert(tgt).second) {
            g.edges.push_back({src, tgt});
            g.predecessors[tgt].insert(src);
        }
    }

    // -- Ensure every node has an entry in adjacency maps (even if empty). --
    // Must come before isolated detection so we can safely use .at().
    for (const auto& node : g.nodes) {
        g.successors[node.id];
        g.predecessors[node.id];
    }

    // -- Isolated nodes: no successors and no predecessors --
    for (const auto& node : g.nodes) {
        const auto& succs = g.successors.at(node.id);
        const auto& preds = g.predecessors.at(node.id);
        if (succs.empty() && preds.empty()) {
            g.isolated.insert(node.id);
        }
    }

    return g;
}

} // namespace bp2::layout::sugiyama
